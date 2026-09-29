#include "test.h"

static b32 test_empty_graph(void)
{
    Manny *graph = manny_create();
	Manny_Execution *execution = NULL;
    CHECK(graph != NULL);
	CHECK_OK(manny_execution_create(graph, &execution));
	CHECK(manny_execution_is_finished(execution));
	manny_execution_destroy(execution);
    manny_destroy(graph);
    return true;
}

static b32 test_linear_graph(void)
{
    Manny *graph = manny_create();
    Manny_Node *compile = test_add_node(graph, "compile");
    Manny_Node *link = test_add_node(graph, "link");
	Manny_Execution *execution = NULL;
	Manny_Execution *second_execution = NULL;
    Manny_Node *node;

    CHECK_OK(manny_add_dependency(graph, link, compile));
	CHECK_OK(manny_execution_create(graph, &execution));
	CHECK_OK(manny_execution_create(graph, &second_execution));

	CHECK(manny_execution_take_ready(execution, &node));
    CHECK(node == compile);
	CHECK(!manny_execution_take_ready(execution, &node));
	CHECK_OK(manny_execution_complete(execution, compile, true));

	CHECK(manny_execution_take_ready(execution, &node));
    CHECK(node == link);
	CHECK_OK(manny_execution_complete(execution, link, true));
	CHECK(manny_execution_is_finished(execution));
	CHECK(!manny_execution_has_failed(execution));

	CHECK(manny_execution_node_state(second_execution, compile) == MANNY_NODE_READY);
	CHECK(manny_execution_node_state(second_execution, link) == MANNY_NODE_PENDING);
	CHECK(manny_execution_take_ready(second_execution, &node));
	CHECK(node == compile);
	CHECK_OK(manny_execution_complete(second_execution, compile, true));
	CHECK(manny_execution_take_ready(second_execution, &node));
	CHECK(node == link);
	CHECK_OK(manny_execution_complete(second_execution, link, true));
	CHECK(manny_execution_is_finished(second_execution));

	manny_execution_destroy(second_execution);
	manny_execution_destroy(execution);
    manny_destroy(graph);
    return true;
}

static b32 test_parallel_fan_in(void)
{
    Manny *graph = manny_create();
    Manny_Node *a = test_add_node(graph, "a");
    Manny_Node *b = test_add_node(graph, "b");
    Manny_Node *link = test_add_node(graph, "link");
	Manny_Execution *execution = NULL;
    Manny_Node *first;
    Manny_Node *second;
    Manny_Node *node;

    CHECK_OK(manny_add_dependency(graph, link, a));
    CHECK_OK(manny_add_dependency(graph, link, b));
	CHECK_OK(manny_execution_create(graph, &execution));

	CHECK(manny_execution_take_ready(execution, &first));
	CHECK(manny_execution_take_ready(execution, &second));
    CHECK(first != second);
	CHECK(!manny_execution_take_ready(execution, &node));

    /* Finishing either node first must not release link early. */
	CHECK_OK(manny_execution_complete(execution, second, true));
	CHECK(!manny_execution_take_ready(execution, &node));
	CHECK_OK(manny_execution_complete(execution, first, true));
	CHECK(manny_execution_take_ready(execution, &node));
    CHECK(node == link);

	CHECK_OK(manny_execution_complete(execution, link, true));
	CHECK(manny_execution_is_finished(execution));
	manny_execution_destroy(execution);
    manny_destroy(graph);
    return true;
}

static b32 test_failure_blocks_dependents(void)
{
    Manny *graph = manny_create();
    Manny_Node *compile = test_add_node(graph, "compile");
    Manny_Node *link = test_add_node(graph, "link");
    Manny_Node *package = test_add_node(graph, "package");
    Manny_Node *independent = test_add_node(graph, "independent");
	Manny_Execution *execution = NULL;
    Manny_Node *first;
    Manny_Node *second;

    CHECK_OK(manny_add_dependency(graph, link, compile));
    CHECK_OK(manny_add_dependency(graph, package, link));
	CHECK_OK(manny_execution_create(graph, &execution));

	CHECK(manny_execution_take_ready(execution, &first));
	CHECK(manny_execution_take_ready(execution, &second));
    CHECK((first == compile && second == independent) ||
          (first == independent && second == compile));

	CHECK_OK(manny_execution_complete(execution, compile, false));
	CHECK(manny_execution_node_state(execution, link) == MANNY_NODE_BLOCKED);
	CHECK(manny_execution_node_state(execution, package) == MANNY_NODE_BLOCKED);
	CHECK(!manny_execution_is_finished(execution));

	CHECK_OK(manny_execution_complete(execution, independent, true));
	CHECK(manny_execution_is_finished(execution));
	CHECK(manny_execution_has_failed(execution));
	manny_execution_destroy(execution);
    manny_destroy(graph);
    return true;
}

static b32 test_cycle_is_rejected(void)
{
    Manny *graph = manny_create();
    Manny_Node *a = test_add_node(graph, "a");
    Manny_Node *b = test_add_node(graph, "b");
    Manny_Node *c = test_add_node(graph, "c");
	Manny_Execution *execution = NULL;

    CHECK_OK(manny_add_dependency(graph, a, b));
    CHECK_OK(manny_add_dependency(graph, b, c));
    CHECK_OK(manny_add_dependency(graph, c, a));
	CHECK(manny_execution_create(graph, &execution) == MANNY_ERROR_CYCLE);
	CHECK(execution == NULL);
    manny_destroy(graph);
    return true;
}

static b32 test_invalid_edges_are_rejected(void)
{
    Manny *graph = manny_create();
    Manny_Node *a = test_add_node(graph, "a");
    Manny_Node *b = test_add_node(graph, "b");

    CHECK(manny_add_dependency(graph, a, a) == MANNY_ERROR_SELF_DEPENDENCY);
    CHECK_OK(manny_add_dependency(graph, a, b));
    CHECK(manny_add_dependency(graph, a, b) == MANNY_ERROR_DUPLICATE_DEPENDENCY);
    manny_destroy(graph);
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
	Manny_Node *dependent;
	Manny_Node *started_nodes[3];
	Manny_Node *completed_nodes[3];
	u32       callback_thread;
	u32       started;
	u32       completed;
	b32       valid;
};

static Manny_Node_Result generic_test_action(Manny_Node_Context *context, void *user_data)
{
	Generic_Action_Test *action = user_data;
	i32 *output = arena_push_zero_aligned(context->arena, sizeof(*output), _Alignof(i32));
	i32 value = action->value;
	action->valid = context->manny && context->node && output &&
		context->execution_data == action->execution &&
		manny_node_user_data(context->node) == action;
	for (u32 i = 0; i < manny_dependency_count(context->node); ++i) {
		Manny_Node_Result dependency = manny_execution_node_result(context->execution, manny_dependency(context->node, i));
		if (!dependency.succeeded || !dependency.output) action->valid = false;
		else value += *(i32 *)dependency.output;
	}
	++action->calls;
	if (output) *output = value;
	return (Manny_Node_Result){
		.output = output,
		.succeeded = action->valid,
		.changed = action->changed,
	};
}

static void generic_test_event(Manny_Event event, void *user_data)
{
	Generic_Execution_Test *execution = user_data;
	if (day_current_thread_id() != execution->callback_thread || !event.node) execution->valid = false;
	if (event.type == MANNY_EVENT_STARTED) {
		if (event.result.output || event.result.succeeded || event.result.changed) execution->valid = false;
		for (u32 i = 0; i < execution->started; ++i) {
			if (execution->started_nodes[i] == event.node) execution->valid = false;
		}
		if (execution->started < ARRAY_COUNT(execution->started_nodes)) execution->started_nodes[execution->started] = event.node;
		else execution->valid = false;
		if (event.node == execution->dependent && execution->completed != 2) execution->valid = false;
		++execution->started;
	}
	else if (event.type == MANNY_EVENT_COMPLETED) {
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

static Manny_Node_Result generic_test_failure(Manny_Node_Context *context, void *user_data)
{
	u32 *calls = user_data;
	(void)context;
	++*calls;
	return (Manny_Node_Result){ .succeeded = false, .changed = true };
}

static b32 test_generic_graph_actions(void)
{
	Manny *graph = manny_create();
	Manny_Node *left = NULL;
	Manny_Node *right = NULL;
	Manny_Node *sum = NULL;
	Manny_Execution *graph_execution = NULL;
	Generic_Execution_Test execution = {
		.callback_thread = day_current_thread_id(),
		.valid = true,
	};
	Generic_Action_Test actions[] = {
		{ .execution = &execution, .value = 3, .changed = true },
		{ .execution = &execution, .value = 5, .changed = false },
		{ .execution = &execution, .value = 1, .changed = true },
	};
	Manny_Node_Result result;
	i32 *graph_value;
	String graph_string;

	CHECK(graph != NULL);
	graph_value = manny_allocate(graph, sizeof(*graph_value), _Alignof(i32));
	graph_string = manny_copy_string(graph, LIT("graph storage"));
	CHECK(graph_value != NULL && graph_string.data != NULL);
	*graph_value = 17;
	CHECK(*graph_value == 17 && string_equal(graph_string, LIT("graph storage")));
	CHECK_OK(manny_add_node(graph, (Manny_Node_Desc){
		.name = LIT("left"),
		.function = generic_test_action,
		.user_data = actions + 0,
	}, &left));
	CHECK_OK(manny_add_node(graph, (Manny_Node_Desc){
		.name = LIT("right"),
		.function = generic_test_action,
		.user_data = actions + 1,
	}, &right));
	CHECK_OK(manny_add_node(graph, (Manny_Node_Desc){
		.name = LIT("sum"),
		.function = generic_test_action,
		.user_data = actions + 2,
	}, &sum));
	CHECK_OK(manny_add_dependency(graph, sum, left));
	CHECK_OK(manny_add_dependency(graph, sum, right));
	execution.dependent = sum;
	CHECK_OK(manny_execution_create(graph, &graph_execution));
	CHECK(manny_execute(graph_execution, (Manny_Exec_Params){
		.worker_count = 2,
		.user_data = &execution,
		.event = generic_test_event,
	}));
	CHECK(execution.valid && execution.started == 3 && execution.completed == 3);
	CHECK(actions[0].valid && actions[0].calls == 1);
	CHECK(actions[1].valid && actions[1].calls == 1);
	CHECK(actions[2].valid && actions[2].calls == 1);
	CHECK(manny_execution_node_state(graph_execution, sum) == MANNY_NODE_SUCCEEDED);
	result = manny_execution_node_result(graph_execution, sum);
	CHECK(result.succeeded && result.changed && result.output);
	CHECK(*(i32 *)result.output == 9);
	CHECK(!manny_execution_node_result(graph_execution, right).changed);
	CHECK(manny_allocate(graph, 1, 1) == NULL);
	CHECK(manny_copy_string(graph, LIT("too late")).data == NULL);
	manny_execution_destroy(graph_execution);
	manny_destroy(graph);

	{
		u32 failed_calls = 0;
		u32 blocked_calls = 0;
		graph = manny_create();
		graph_execution = NULL;
		CHECK(graph != NULL);
		CHECK_OK(manny_add_node(graph, (Manny_Node_Desc){
			.name = LIT("failure"),
			.function = generic_test_failure,
			.user_data = &failed_calls,
		}, &left));
		CHECK_OK(manny_add_node(graph, (Manny_Node_Desc){
			.name = LIT("blocked"),
			.function = generic_test_failure,
			.user_data = &blocked_calls,
		}, &right));
		CHECK_OK(manny_add_dependency(graph, right, left));
		CHECK_OK(manny_execution_create(graph, &graph_execution));
		CHECK(!manny_execute(graph_execution, (Manny_Exec_Params){ .worker_count = 2 }));
		CHECK(failed_calls == 1 && blocked_calls == 0);
		CHECK(manny_execution_node_state(graph_execution, left) == MANNY_NODE_FAILED);
		CHECK(manny_execution_node_state(graph_execution, right) == MANNY_NODE_BLOCKED);
		CHECK(!manny_execution_node_result(graph_execution, left).changed);
		manny_execution_destroy(graph_execution);
		manny_destroy(graph);
	}
	return true;
}


int main(void)
{
	static const Manny_Test tests[] = {
		MANNY_TEST(test_empty_graph),
		MANNY_TEST(test_linear_graph),
		MANNY_TEST(test_parallel_fan_in),
		MANNY_TEST(test_failure_blocks_dependents),
		MANNY_TEST(test_cycle_is_rejected),
		MANNY_TEST(test_invalid_edges_are_rejected),
		MANNY_TEST(test_generic_graph_actions),
	};
	return test_run_suite("graph", tests, ARRAY_COUNT(tests));
}
