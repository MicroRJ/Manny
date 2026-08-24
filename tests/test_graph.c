#include "test.h"

static b32 test_empty_graph(void)
{
    Bob *graph = bob_create();
	Bob_Execution *execution = NULL;
    CHECK(graph != NULL);
	CHECK_OK(bob_execution_create(graph, &execution));
	CHECK(bob_execution_is_finished(execution));
	bob_execution_destroy(execution);
    bob_destroy(graph);
    return true;
}

static b32 test_linear_graph(void)
{
    Bob *graph = bob_create();
    Bob_Node *compile = test_add_node(graph, "compile");
    Bob_Node *link = test_add_node(graph, "link");
	Bob_Execution *execution = NULL;
	Bob_Execution *second_execution = NULL;
    Bob_Node *node;

    CHECK_OK(bob_add_dependency(graph, link, compile));
	CHECK_OK(bob_execution_create(graph, &execution));
	CHECK_OK(bob_execution_create(graph, &second_execution));

	CHECK(bob_execution_take_ready(execution, &node));
    CHECK(node == compile);
	CHECK(!bob_execution_take_ready(execution, &node));
	CHECK_OK(bob_execution_complete(execution, compile, true));

	CHECK(bob_execution_take_ready(execution, &node));
    CHECK(node == link);
	CHECK_OK(bob_execution_complete(execution, link, true));
	CHECK(bob_execution_is_finished(execution));
	CHECK(!bob_execution_has_failed(execution));

	CHECK(bob_execution_node_state(second_execution, compile) == BOB_NODE_READY);
	CHECK(bob_execution_node_state(second_execution, link) == BOB_NODE_PENDING);
	CHECK(bob_execution_take_ready(second_execution, &node));
	CHECK(node == compile);
	CHECK_OK(bob_execution_complete(second_execution, compile, true));
	CHECK(bob_execution_take_ready(second_execution, &node));
	CHECK(node == link);
	CHECK_OK(bob_execution_complete(second_execution, link, true));
	CHECK(bob_execution_is_finished(second_execution));

	bob_execution_destroy(second_execution);
	bob_execution_destroy(execution);
    bob_destroy(graph);
    return true;
}

static b32 test_parallel_fan_in(void)
{
    Bob *graph = bob_create();
    Bob_Node *a = test_add_node(graph, "a");
    Bob_Node *b = test_add_node(graph, "b");
    Bob_Node *link = test_add_node(graph, "link");
	Bob_Execution *execution = NULL;
    Bob_Node *first;
    Bob_Node *second;
    Bob_Node *node;

    CHECK_OK(bob_add_dependency(graph, link, a));
    CHECK_OK(bob_add_dependency(graph, link, b));
	CHECK_OK(bob_execution_create(graph, &execution));

	CHECK(bob_execution_take_ready(execution, &first));
	CHECK(bob_execution_take_ready(execution, &second));
    CHECK(first != second);
	CHECK(!bob_execution_take_ready(execution, &node));

    /* Finishing either node first must not release link early. */
	CHECK_OK(bob_execution_complete(execution, second, true));
	CHECK(!bob_execution_take_ready(execution, &node));
	CHECK_OK(bob_execution_complete(execution, first, true));
	CHECK(bob_execution_take_ready(execution, &node));
    CHECK(node == link);

	CHECK_OK(bob_execution_complete(execution, link, true));
	CHECK(bob_execution_is_finished(execution));
	bob_execution_destroy(execution);
    bob_destroy(graph);
    return true;
}

static b32 test_failure_blocks_dependents(void)
{
    Bob *graph = bob_create();
    Bob_Node *compile = test_add_node(graph, "compile");
    Bob_Node *link = test_add_node(graph, "link");
    Bob_Node *package = test_add_node(graph, "package");
    Bob_Node *independent = test_add_node(graph, "independent");
	Bob_Execution *execution = NULL;
    Bob_Node *first;
    Bob_Node *second;

    CHECK_OK(bob_add_dependency(graph, link, compile));
    CHECK_OK(bob_add_dependency(graph, package, link));
	CHECK_OK(bob_execution_create(graph, &execution));

	CHECK(bob_execution_take_ready(execution, &first));
	CHECK(bob_execution_take_ready(execution, &second));
    CHECK((first == compile && second == independent) ||
          (first == independent && second == compile));

	CHECK_OK(bob_execution_complete(execution, compile, false));
	CHECK(bob_execution_node_state(execution, link) == BOB_NODE_BLOCKED);
	CHECK(bob_execution_node_state(execution, package) == BOB_NODE_BLOCKED);
	CHECK(!bob_execution_is_finished(execution));

	CHECK_OK(bob_execution_complete(execution, independent, true));
	CHECK(bob_execution_is_finished(execution));
	CHECK(bob_execution_has_failed(execution));
	bob_execution_destroy(execution);
    bob_destroy(graph);
    return true;
}

static b32 test_cycle_is_rejected(void)
{
    Bob *graph = bob_create();
    Bob_Node *a = test_add_node(graph, "a");
    Bob_Node *b = test_add_node(graph, "b");
    Bob_Node *c = test_add_node(graph, "c");
	Bob_Execution *execution = NULL;

    CHECK_OK(bob_add_dependency(graph, a, b));
    CHECK_OK(bob_add_dependency(graph, b, c));
    CHECK_OK(bob_add_dependency(graph, c, a));
	CHECK(bob_execution_create(graph, &execution) == BOB_ERROR_CYCLE);
	CHECK(execution == NULL);
    bob_destroy(graph);
    return true;
}

static b32 test_invalid_edges_are_rejected(void)
{
    Bob *graph = bob_create();
    Bob_Node *a = test_add_node(graph, "a");
    Bob_Node *b = test_add_node(graph, "b");

    CHECK(bob_add_dependency(graph, a, a) == BOB_ERROR_SELF_DEPENDENCY);
    CHECK_OK(bob_add_dependency(graph, a, b));
    CHECK(bob_add_dependency(graph, a, b) == BOB_ERROR_DUPLICATE_DEPENDENCY);
    bob_destroy(graph);
    return true;
}

typedef struct Generic_Execution_Test Generic_Execution_Test;

typedef struct Generic_Action_Test
{
	Generic_Execution_Test *execution;
	u32                     calls;
	i32                     value;
	b32                     changed;
	b32                     valid;
}
Generic_Action_Test;

struct Generic_Execution_Test
{
	Bob_Node *dependent;
	Bob_Node *started_nodes[3];
	Bob_Node *completed_nodes[3];
	u32       callback_thread;
	u32       started;
	u32       completed;
	b32       valid;
};

static Bob_Node_Result generic_test_action(Bob_Node_Context *context, void *user_data)
{
	Generic_Action_Test *action = user_data;
	i32 *output = arena_push_zero_aligned(context->arena, sizeof(*output), _Alignof(i32));
	i32 value = action->value;
	action->valid = context->bob && context->node && output &&
		context->execution_data == action->execution &&
		bob_node_user_data(context->node) == action;
	for (u32 i = 0; i < bob_dependency_count(context->node); ++i) {
		Bob_Node_Result dependency = bob_execution_node_result(context->execution, bob_dependency(context->node, i));
		if (!dependency.succeeded || !dependency.output) action->valid = false;
		else value += *(i32 *)dependency.output;
	}
	++action->calls;
	if (output) *output = value;
	return (Bob_Node_Result){
		.output = output,
		.succeeded = action->valid,
		.changed = action->changed,
	};
}

static void generic_test_event(Bob_Event event, void *user_data)
{
	Generic_Execution_Test *execution = user_data;
	if (platform_current_thread_id() != execution->callback_thread || !event.node) execution->valid = false;
	if (event.type == BOB_EVENT_STARTED) {
		if (event.result.output || event.result.succeeded || event.result.changed) execution->valid = false;
		for (u32 i = 0; i < execution->started; ++i) {
			if (execution->started_nodes[i] == event.node) execution->valid = false;
		}
		if (execution->started < ARRAY_COUNT(execution->started_nodes)) execution->started_nodes[execution->started] = event.node;
		else execution->valid = false;
		if (event.node == execution->dependent && execution->completed != 2) execution->valid = false;
		++execution->started;
	}
	else if (event.type == BOB_EVENT_COMPLETED) {
		b32 was_started = false;
		if (!event.result.succeeded || !event.result.output) execution->valid = false;
		for (u32 i = 0; i < execution->started && i < ARRAY_COUNT(execution->started_nodes); ++i) {
			if (execution->started_nodes[i] == event.node) was_started = true;
		}
		for (u32 i = 0; i < execution->completed && i < ARRAY_COUNT(execution->completed_nodes); ++i) {
			if (execution->completed_nodes[i] == event.node) execution->valid = false;
		}
		if (!was_started) execution->valid = false;
		if (execution->completed < ARRAY_COUNT(execution->completed_nodes)) execution->completed_nodes[execution->completed] = event.node;
		else execution->valid = false;
		++execution->completed;
	}
	else execution->valid = false;
}

static Bob_Node_Result generic_test_failure(Bob_Node_Context *context, void *user_data)
{
	u32 *calls = user_data;
	(void)context;
	++*calls;
	return (Bob_Node_Result){ .succeeded = false, .changed = true };
}

static b32 test_generic_graph_actions(void)
{
	Bob *graph = bob_create();
	Bob_Node *left = NULL;
	Bob_Node *right = NULL;
	Bob_Node *sum = NULL;
	Bob_Execution *graph_execution = NULL;
	Generic_Execution_Test execution = {
		.callback_thread = platform_current_thread_id(),
		.valid = true,
	};
	Generic_Action_Test actions[] = {
		{ .execution = &execution, .value = 3, .changed = true },
		{ .execution = &execution, .value = 5, .changed = false },
		{ .execution = &execution, .value = 1, .changed = true },
	};
	Bob_Node_Result result;
	i32 *graph_value;
	String graph_string;

	CHECK(graph != NULL);
	graph_value = bob_allocate(graph, sizeof(*graph_value), _Alignof(i32));
	graph_string = bob_copy_string(graph, LIT("graph storage"));
	CHECK(graph_value != NULL && graph_string.data != NULL);
	*graph_value = 17;
	CHECK(*graph_value == 17 && string_equal(graph_string, LIT("graph storage")));
	CHECK_OK(bob_add_node(graph, (Bob_Node_Desc){
		.name = LIT("left"),
		.function = generic_test_action,
		.user_data = actions + 0,
	}, &left));
	CHECK_OK(bob_add_node(graph, (Bob_Node_Desc){
		.name = LIT("right"),
		.function = generic_test_action,
		.user_data = actions + 1,
	}, &right));
	CHECK_OK(bob_add_node(graph, (Bob_Node_Desc){
		.name = LIT("sum"),
		.function = generic_test_action,
		.user_data = actions + 2,
	}, &sum));
	CHECK_OK(bob_add_dependency(graph, sum, left));
	CHECK_OK(bob_add_dependency(graph, sum, right));
	execution.dependent = sum;
	CHECK_OK(bob_execution_create(graph, &graph_execution));
	CHECK(bob_execute(graph_execution, (Bob_Exec_Params){
		.worker_count = 2,
		.user_data = &execution,
		.event = generic_test_event,
	}));
	CHECK(execution.valid && execution.started == 3 && execution.completed == 3);
	CHECK(actions[0].valid && actions[0].calls == 1);
	CHECK(actions[1].valid && actions[1].calls == 1);
	CHECK(actions[2].valid && actions[2].calls == 1);
	CHECK(bob_execution_node_state(graph_execution, sum) == BOB_NODE_SUCCEEDED);
	result = bob_execution_node_result(graph_execution, sum);
	CHECK(result.succeeded && result.changed && result.output);
	CHECK(*(i32 *)result.output == 9);
	CHECK(!bob_execution_node_result(graph_execution, right).changed);
	CHECK(bob_allocate(graph, 1, 1) == NULL);
	CHECK(bob_copy_string(graph, LIT("too late")).data == NULL);
	bob_execution_destroy(graph_execution);
	bob_destroy(graph);

	{
		u32 failed_calls = 0;
		u32 blocked_calls = 0;
		graph = bob_create();
		graph_execution = NULL;
		CHECK(graph != NULL);
		CHECK_OK(bob_add_node(graph, (Bob_Node_Desc){
			.name = LIT("failure"),
			.function = generic_test_failure,
			.user_data = &failed_calls,
		}, &left));
		CHECK_OK(bob_add_node(graph, (Bob_Node_Desc){
			.name = LIT("blocked"),
			.function = generic_test_failure,
			.user_data = &blocked_calls,
		}, &right));
		CHECK_OK(bob_add_dependency(graph, right, left));
		CHECK_OK(bob_execution_create(graph, &graph_execution));
		CHECK(!bob_execute(graph_execution, (Bob_Exec_Params){ .worker_count = 2 }));
		CHECK(failed_calls == 1 && blocked_calls == 0);
		CHECK(bob_execution_node_state(graph_execution, left) == BOB_NODE_FAILED);
		CHECK(bob_execution_node_state(graph_execution, right) == BOB_NODE_BLOCKED);
		CHECK(!bob_execution_node_result(graph_execution, left).changed);
		bob_execution_destroy(graph_execution);
		bob_destroy(graph);
	}
	return true;
}


int main(void)
{
	static const Bob_Test tests[] = {
		BOB_TEST(test_empty_graph),
		BOB_TEST(test_linear_graph),
		BOB_TEST(test_parallel_fan_in),
		BOB_TEST(test_failure_blocks_dependents),
		BOB_TEST(test_cycle_is_rejected),
		BOB_TEST(test_invalid_edges_are_rejected),
		BOB_TEST(test_generic_graph_actions),
	};
	return test_run_suite("graph", tests, ARRAY_COUNT(tests));
}
