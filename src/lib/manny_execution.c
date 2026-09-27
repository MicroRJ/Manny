#include "manny_internal.h"
#include "logger.h"
#include "platform.h"
#include "profiler.h"

typedef struct Manny_Execution_Node
{
	u32             unfinished_dependencies;
	Manny_Node_Result result;
	Manny_Node_Status state;
}
Manny_Execution_Node;

struct Manny_Execution
{
	Arena               arena;
	Manny                *manny;
	Manny_Execution_Node *nodes;

	// TODO(RJ): this is scoped to the execution function, but we keep here for tests
	Manny_Node           **ready;
	u32                  ready_count;
	u32                  ready_head;

	u32                  terminal_count;
	Arena               *output_arenas;
	u32                  output_arena_count;
	b32                  failed;
	b32                  automatically_driven;
	b32                  manually_driven;
};

static Manny_Execution_Node *execution_node(Manny_Execution *execution, const Manny_Node *node)
{
	if (!execution || !manny_valid_node(execution->manny, node)) return NULL;
	return execution->nodes + node->index;
}

static const Manny_Execution_Node *execution_node_const(const Manny_Execution *execution, const Manny_Node *node)
{
	if (!execution || !manny_valid_node(execution->manny, node)) return NULL;
	return execution->nodes + node->index;
}

static void enqueue_ready(Manny_Execution *execution, Manny_Node *node)
{
	execution->ready[execution->ready_count++] = node;
	execution->nodes[node->index].state = MANNY_NODE_READY;
}

Manny_Error manny_execution_create(Manny *manny, Manny_Execution **execution_out)
{
	Arena arena;
	Manny_Execution *execution;
	u32 visited = 0;
	if (!manny || !execution_out) return MANNY_ERROR_INVALID_NODE;
	*execution_out = NULL;
	arena = arena_create(0);
	if (!arena.data) return MANNY_ERROR_OUT_OF_MEMORY;
	arena_set_name(&arena, "Manny execution");
	execution = arena_push_zero_aligned(&arena, sizeof(*execution), _Alignof(Manny_Execution));
	if (!execution) goto out_of_memory;
	execution->arena = arena;
	execution->manny = manny;
	if (manny->node_count) {
		execution->nodes = arena_push_zero_aligned(&execution->arena, (u64)manny->node_count * sizeof(*execution->nodes), _Alignof(Manny_Execution_Node));
		execution->ready = arena_push_zero_aligned(&execution->arena, (u64)manny->node_count * sizeof(*execution->ready), _Alignof(Manny_Node *));
		if (!execution->nodes || !execution->ready) goto out_of_memory;
	}

	for (u32 i = 0; i < manny->node_count; ++i) {
		execution->nodes[i].unfinished_dependencies = manny->nodes[i]->dependencies.count;
		if (execution->nodes[i].unfinished_dependencies == 0) execution->ready[execution->ready_count++] = manny->nodes[i];
	}
	while (visited < execution->ready_count) {
		Manny_Node *node = execution->ready[visited++];
		for (u32 i = 0; i < node->dependents.count; ++i) {
			Manny_Node *dependent = node->dependents.items[i];
			Manny_Execution_Node *dependent_state = execution->nodes + dependent->index;
			--dependent_state->unfinished_dependencies;
			if (dependent_state->unfinished_dependencies == 0) execution->ready[execution->ready_count++] = dependent;
		}
	}
	if (visited != manny->node_count) {
		arena = execution->arena;
		arena_destroy(&arena);
		return MANNY_ERROR_CYCLE;
	}

	execution->ready_count = 0;
	for (u32 i = 0; i < manny->node_count; ++i) {
		Manny_Node *node = manny->nodes[i];
		execution->nodes[i] = (Manny_Execution_Node){ .unfinished_dependencies = node->dependencies.count };
		if (node->dependencies.count == 0) enqueue_ready(execution, node);
	}
	manny->sealed = true;
	++manny->execution_count;
	*execution_out = execution;
	return MANNY_OK;

out_of_memory:
	arena_destroy(&arena);
	return MANNY_ERROR_OUT_OF_MEMORY;
}

void manny_execution_destroy(Manny_Execution *execution)
{
	Arena arena;
	if (!execution) return;
	for (u32 i = 0; i < execution->output_arena_count; ++i) arena_destroy(execution->output_arenas + i);
	ASSERT(execution->manny);
	ASSERT(execution->manny->execution_count > 0);
	--execution->manny->execution_count;
	arena = execution->arena;
	arena_destroy(&arena);
}

static b32 take_ready(Manny_Execution *execution, Manny_Node **node_out)
{
	Manny_Node *node;
	if (!execution || !node_out || execution->ready_head == execution->ready_count) return false;
	node = execution->ready[execution->ready_head++];
	execution->nodes[node->index].state = MANNY_NODE_RUNNING;
	*node_out = node;
	return true;
}

b32 manny_execution_take_ready(Manny_Execution *execution, Manny_Node **node_out)
{
	if (!execution || execution->automatically_driven) return false;
	execution->manually_driven = true;
	return take_ready(execution, node_out);
}

static void block_node_and_dependents(Manny_Execution *execution, Manny_Node *node)
{
	Manny_Execution_Node *state = execution->nodes + node->index;
	if (state->state == MANNY_NODE_BLOCKED || state->state == MANNY_NODE_SUCCEEDED || state->state == MANNY_NODE_FAILED) return;
	state->state = MANNY_NODE_BLOCKED;
	state->result = (Manny_Node_Result){0};
	++execution->terminal_count;
	for (u32 i = 0; i < node->dependents.count; ++i) block_node_and_dependents(execution, node->dependents.items[i]);
}

static Manny_Error complete_result(Manny_Execution *execution, Manny_Node *node, Manny_Node_Result result)
{
	Manny_Execution_Node *state = execution_node(execution, node);
	if (!state) return MANNY_ERROR_INVALID_NODE;
	if (state->state != MANNY_NODE_RUNNING) return MANNY_ERROR_INVALID_STATE;
	state->result = result;
	state->state = result.succeeded ? MANNY_NODE_SUCCEEDED : MANNY_NODE_FAILED;
	++execution->terminal_count;
	if (!result.succeeded) {
		execution->failed = true;
		for (u32 i = 0; i < node->dependents.count; ++i) block_node_and_dependents(execution, node->dependents.items[i]);
		return MANNY_OK;
	}
	for (u32 i = 0; i < node->dependents.count; ++i) {
		Manny_Node *dependent = node->dependents.items[i];
		Manny_Execution_Node *dependent_state = execution->nodes + dependent->index;
		if (dependent_state->state != MANNY_NODE_PENDING) continue;
		--dependent_state->unfinished_dependencies;
		if (dependent_state->unfinished_dependencies == 0) enqueue_ready(execution, dependent);
	}
	return MANNY_OK;
}

Manny_Error manny_execution_complete_result(Manny_Execution *execution, Manny_Node *node, Manny_Node_Result result)
{
	if (!execution) return MANNY_ERROR_INVALID_NODE;
	if (execution->automatically_driven) return MANNY_ERROR_INVALID_STATE;
	execution->manually_driven = true;
	return complete_result(execution, node, result);
}

Manny_Error manny_execution_complete(Manny_Execution *execution, Manny_Node *node, b32 succeeded)
{
	return manny_execution_complete_result(execution, node, (Manny_Node_Result){
		.succeeded = succeeded,
		.changed = succeeded,
	});
}

b32 manny_execution_is_finished(const Manny_Execution *execution)
{
	ASSERT(execution);
	return execution->terminal_count == execution->manny->node_count;
}

b32 manny_execution_has_failed(const Manny_Execution *execution)
{
	ASSERT(execution);
	return execution->failed;
}

Manny_Node_Status manny_execution_node_state(const Manny_Execution *execution, const Manny_Node *node)
{
	const Manny_Execution_Node *state = execution_node_const(execution, node);
	return state ? state->state : MANNY_NODE_PENDING;
}

Manny_Node_Result manny_execution_node_result(const Manny_Execution *execution, const Manny_Node *node)
{
	const Manny_Execution_Node *state = execution_node_const(execution, node);
	return state ? state->result : (Manny_Node_Result){0};
}

typedef struct Manny_Executor Manny_Executor;

typedef struct Manny_Worker
{
	Manny_Executor   *executor;
	Platform_Thread thread;
	Arena          *output;
}
Manny_Worker;

struct Manny_Executor
{
	Manny_Execution        *execution;
	Manny_Exec_Params       options;
	Manny_Worker           *workers;
	u32                   worker_count;
	u32                   thread_count;
	u32                   running;
	Manny_Node            **work;
	u32                   work_count;
	Manny_Event            *events;
	u32                   event_capacity;
	u32                   event_head;
	u32                   event_count;
	Platform_Mutex        mutex;
	Platform_Condition    work_available;
	Platform_Condition    event_available;
	b32                   stopping;
};

static void request_stop_locked(Manny_Executor *executor)
{
	executor->stopping = true;
	platform_broadcast_condition(&executor->work_available);
	platform_broadcast_condition(&executor->event_available);
}

static b32 enqueue_event_locked(Manny_Executor *executor, Manny_Event event)
{
	if (executor->event_count == executor->event_capacity) return false;
	u32 index = (executor->event_head + executor->event_count) % executor->event_capacity;
	executor->events[index] = event;
	++executor->event_count;
	platform_signal_condition(&executor->event_available);
	return true;
}

static b32 dequeue_event_locked(Manny_Executor *executor, Manny_Event *event)
{
	if (executor->event_count == 0) return false;
	*event = executor->events[executor->event_head];
	executor->event_head = (executor->event_head + 1) % executor->event_capacity;
	--executor->event_count;
	return true;
}

static u32 worker_main(void *data)
{
	Manny_Worker *worker = data;
	Manny_Executor *executor = worker->executor;
	for (;;) {
		Manny_Node *node;
		Manny_Event completion;
		Manny_Node_Context context;

		platform_lock_mutex(&executor->mutex);
		while (!executor->stopping && executor->work_count == 0) {
			if (platform_wait_condition(&executor->work_available, &executor->mutex).error) {
				log_fatal("failed waiting for worker queue");
				request_stop_locked(executor);
			}
		}
		if (executor->stopping) {
			platform_unlock_mutex(&executor->mutex);
			break;
		}

		node = executor->work[--executor->work_count];
		if (!enqueue_event_locked(executor, (Manny_Event){ .type = MANNY_EVENT_STARTED, .node = node })) {
			log_fatal("executor event queue exhausted");
			request_stop_locked(executor);
			platform_unlock_mutex(&executor->mutex);
			break;
		}
		platform_unlock_mutex(&executor->mutex);

		completion = (Manny_Event){ .type = MANNY_EVENT_COMPLETED, .node = node };
		context = (Manny_Node_Context){
			.execution = executor->execution,
			.manny = executor->execution->manny,
			.node = node,
			.arena = worker->output,
			.execution_data = executor->options.user_data,
		};
		{
			Profile_Scope scope = profile_scope_begin("node action");
			completion.result = node->function(&context, node->user_data);
			profile_scope_end(&scope);
		}
		if (!completion.result.succeeded) completion.result.changed = false;

		platform_lock_mutex(&executor->mutex);
		if (!enqueue_event_locked(executor, completion)) {
			log_fatal("executor event queue exhausted");
			request_stop_locked(executor);
		}
		platform_unlock_mutex(&executor->mutex);
	}
	destroy_global_scratch();
	return 0;
}

static void dispatch_ready(Manny_Executor *executor)
{
	u32 previous_work_count;
	Manny_Node *node;
	platform_lock_mutex(&executor->mutex);
	previous_work_count = executor->work_count;
	while (executor->running < executor->worker_count && take_ready(executor->execution, &node)) {
		executor->work[executor->work_count++] = node;
		++executor->running;
	}
	if (executor->work_count > previous_work_count) platform_broadcast_condition(&executor->work_available);
	platform_unlock_mutex(&executor->mutex);
}

// TODO(RJ): we could split this into three functions:
//
// manny_execution_begin()
// while (manny_execution_event(& event)) {}
// manny_execution_end()
//
// Potentially one that does all 3 things, the point is, it would get of the callback!
//
b32 manny_execute(Manny_Execution *execution, Manny_Exec_Params options)
{
	Manny_Executor executor = { .execution = execution, .options = options };
	Scratch scratch;
	b32 internal_error = false;
	b32 synchronization_initialized = false;
	u32 node_count;
	if (!execution || execution->automatically_driven || execution->manually_driven || options.worker_count == 0) return false;
	execution->automatically_driven = true;
	node_count = execution->manny->node_count;
	for (u32 i = 0; i < node_count; ++i) {
		Manny_Node *node = execution->manny->nodes[i];
		if (!node->function) {
			log_error("node has no action: %s", manny_node_name(node));
			return false;
		}
	}
	if (manny_execution_is_finished(execution)) return true;
	if (options.worker_count > node_count) options.worker_count = node_count;
	executor.options = options;
	executor.worker_count = options.worker_count;
	scratch = begin_scratch();
	executor.workers = arena_push_zero_aligned(scratch.arena, executor.worker_count * sizeof(*executor.workers), _Alignof(Manny_Worker));
	executor.work = arena_push_zero_aligned(scratch.arena, executor.worker_count * sizeof(*executor.work), _Alignof(Manny_Node *));
	if (executor.worker_count > UINT32_MAX / 2) {
		internal_error = true;
		goto cleanup;
	}
	executor.event_capacity = executor.worker_count * 2;
	executor.events = arena_push_zero_aligned(scratch.arena, executor.event_capacity * sizeof(*executor.events), _Alignof(Manny_Event));
	execution->output_arenas = arena_push_zero_aligned(&execution->arena, executor.worker_count * sizeof(*execution->output_arenas), _Alignof(Arena));
	internal_error = !executor.workers || !executor.work || !executor.events || !execution->output_arenas;
	if (internal_error) goto cleanup;

	platform_init_mutex(&executor.mutex);
	platform_init_condition(&executor.work_available);
	platform_init_condition(&executor.event_available);
	synchronization_initialized = true;
	for (u32 i = 0; i < executor.worker_count; ++i) {
		Manny_Worker *worker = executor.workers + i;
		worker->executor = &executor;
		worker->output = execution->output_arenas + i;
		*worker->output = arena_create(MEGABYTES(256));
		arena_set_name(worker->output, "worker output");
		internal_error = !worker->output->data;
		if (internal_error) goto cleanup;
		++execution->output_arena_count;
		Platform_Thread_Start_Result start = platform_start_thread(worker_main, worker);
		internal_error = start.error != PLATFORM_ERROR_NONE;
		if (internal_error) goto cleanup;
		worker->thread = start.thread;
		++executor.thread_count;
	}

	while (!internal_error && !manny_execution_is_finished(execution)) {
		Manny_Event event = {0};
		b32 has_event = false;
		dispatch_ready(&executor);
		if (manny_execution_is_finished(execution)) break;
		if (executor.running == 0) {
			internal_error = true;
			break;
		}
		platform_lock_mutex(&executor.mutex);
		while (!executor.stopping && executor.event_count == 0) {
			if (platform_wait_condition(&executor.event_available, &executor.mutex).error) {
				log_fatal("failed waiting for worker event");
				request_stop_locked(&executor);
			}
		}
		has_event = dequeue_event_locked(&executor, &event);
		platform_unlock_mutex(&executor.mutex);
		if (!has_event) {
			internal_error = true;
			break;
		}
		if (event.type == MANNY_EVENT_COMPLETED) {
			if (complete_result(execution, event.node, event.result) != MANNY_OK) internal_error = true;
			--executor.running;
			if (!internal_error) dispatch_ready(&executor);
		}
		else if (event.type != MANNY_EVENT_STARTED) internal_error = true;
		if (executor.options.event) executor.options.event(event, executor.options.user_data);
	}

cleanup:
	if (synchronization_initialized) {
		platform_lock_mutex(&executor.mutex);
		request_stop_locked(&executor);
		platform_unlock_mutex(&executor.mutex);
	}
	for (u32 i = 0; i < executor.thread_count; ++i) {
		platform_join_thread(executor.workers[i].thread);
		platform_close_thread(&executor.workers[i].thread);
	}
	if (synchronization_initialized) {
		platform_destroy_condition(&executor.event_available);
		platform_destroy_condition(&executor.work_available);
		platform_destroy_mutex(&executor.mutex);
	}
	end_scratch(scratch);
	return !internal_error && !manny_execution_has_failed(execution);
}
