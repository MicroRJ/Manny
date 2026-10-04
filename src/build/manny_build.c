#include "manny_build_internal.h"
#include "build_record.h"
#include "compiler_command.h"
#include "logger.h"
#include "make_depfile.h"
#include "platform_adapter.h"
#include "profiler.h"
#include "blake3.h"

#include <stdio.h>
#include <string.h>

#define MANNY_BUILD_STATE_PATH ".manny/state"

typedef enum Manny_Rebuild_Reason
{
	MANNY_REBUILD_UP_TO_DATE,
	MANNY_REBUILD_NO_OUTPUTS,
	MANNY_REBUILD_OUTPUT_MISSING,
	MANNY_REBUILD_INPUT_MISSING,
	MANNY_REBUILD_STATE_MISSING,
	MANNY_REBUILD_STATE_CHANGED,
	MANNY_REBUILD_FINGERPRINT_CHANGED,
	MANNY_REBUILD_DEPENDENCY_MISSING,
	MANNY_REBUILD_DEPENDENCY_CHANGED,
	MANNY_REBUILD_INPUT_NEWER,
}
Manny_Rebuild_Reason;

struct Manny_Build
{
	Arena          arena;
	Manny           *graph;
	Manny_Execution *execution;
	Manny_Interner  *interner;
	Manny_Path       root;
};

typedef struct Build_Task
{
	// Copy of the user's command line.
	String           command_line;
	// The processed command line, once other options are injected.
	String           execution_command_line;
	// Derived compiler metadata from the command line.
	Compiler_Command compiler;

	Manny_Path_Array   inputs;
	Manny_Path_Array   outputs;
	Manny_Path_Array   include_directories;
	Manny_Path         execution_directory;
	Manny_Path         dependency_file;

	Manny_Fingerprint  fingerprint;

	// Whether the compiler supports deps files and the task has any outputs.
	b32              tracks_dependencies;

	// If the task is transparent it is never marked as changed
	b32              transparent;
}
Build_Task;

typedef struct Manny_Rebuild_Decision
{
	Manny_Rebuild_Reason reason;
	String             path;
	String             reference;
	const Manny_Node    *dependency;
	b32                rebuild;
}
Manny_Rebuild_Decision;

typedef struct Manny_Build_Completion
{
	Manny_Build                   *build;
	Manny_Node                    *node;
	Build_Task                  *task;
	Manny_Platform_Process_Result  process;
	Manny_Path_Array               dependencies;
	Manny_Rebuild_Decision         decision;
	b32                          dependency_state_valid;
}
Manny_Build_Completion;

typedef struct Manny_Builder
{
	Manny_Build          *build;
	Manny                *manny;
	Manny_Path            state_path;

	Manny_Build_Recorder   record_stream;
	Manny_Build_Snapshot record_snapshot;

	Arena               state_arena;
	void               *event_user_data;
	Manny_Event_Function *event;
	u32                 task_count;
	u32                 completed_task_count;
	// TODO(RJ): remove this from here!
	b32                 state_tracking;

	b32                 state_changed;
	b32                 explain;
	b32                 internal_error;
}
Manny_Builder;




static b32 manny_path_is_absolute(String path)
{
	return path.size > 0 && (path.data[0] == '/' || path.data[0] == '\\' ||
		(path.size >= 3 && path.data[1] == ':' && (path.data[2] == '/' || path.data[2] == '\\')));
}

static b32 manny_path_absolute(Arena *arena, String path, String *result)
{
	return !dy_get_absolute_path(arena, path, result).error;
}

static String manny_path_normalize_separators(String path)
{
	u64 write = 0;
	for (u64 read = 0; read < path.size; ++read) {
		char character = path.data[read] == '\\' ? '/' : path.data[read];
		b32 preserve_unc_prefix = write < 2 && read < 2 && character == '/';
		if (character == '/' && write > 0 && path.data[write - 1] == '/' && !preserve_unc_prefix) continue;
		path.data[write++] = character;
	}
	if (write >= 2 && path.data[1] == ':' && path.data[0] >= 'a' && path.data[0] <= 'z') path.data[0] -= 'a' - 'A';
	b32 root = write == 1 && path.data[0] == '/';
	b32 drive_root = write == 3 && path.data[1] == ':' && path.data[2] == '/';
	if (write > 0 && path.data[write - 1] == '/' && !root && !drive_root) --write;
	path.data[write] = 0;
	path.size = write;
	return path;
}

b32 manny_path_resolve(Manny_Build *build, Manny_Path directory, String source, Manny_Path *result)
{
	if (!build || !result || !source.data || source.size == 0) return false;
	Scratch scratch = begin_scratch();
	String candidate = source;
	if (!manny_path_is_absolute(source)) {
		String base = manny_path_string(build, directory);
		if (!base.data) goto failure;
		void *start = arena_top(scratch.arena);
		arena_append_str(scratch.arena, base);
		if (base.size && base.data[base.size - 1] != '/') arena_append_char(scratch.arena, '/');
		arena_append_str(scratch.arena, source);
		candidate = arena_string_from(scratch.arena, start);
		arena_finalize_string(scratch.arena, candidate);
	}
	String absolute;
	if (!manny_path_absolute(scratch.arena, candidate, &absolute)) goto failure;
	absolute = manny_path_normalize_separators(absolute);
	if (!absolute.data || absolute.size == 0) goto failure;
	Manny_Atom atom = manny_interner_intern(build->interner, absolute);
	if (!atom.id) goto failure;
	*result = (Manny_Path){ atom };
	end_scratch(scratch);
	return true;

failure:
	end_scratch(scratch);
	return false;
}

String manny_path_string(const Manny_Build *build, Manny_Path path)
{
	ASSERT(build);
	return manny_interner_string(build->interner, path.atom);
}

b32 manny_path_is_valid(Manny_Path path)
{
	return manny_atom_is_valid(path.atom);
}

Manny_Path manny_build_root(const Manny_Build *build)
{
	return build ? build->root : (Manny_Path){0};
}

Manny_Build *manny_build_create_at(String root)
{
	Arena arena = arena_create(0);
	if (!arena.data) return NULL;
	arena_set_name(&arena, "Manny build");

	Manny_Build *build = arena_push_zero_aligned(&arena, sizeof(*build), _Alignof(Manny_Build));
	if (!build) {
		logger_log_string(LOG_LEVEL_ERROR, "build", LIT("cannot initiate build, could not allocate memory"));
		arena_destroy(&arena);
		return NULL;
	}

	build->arena = arena;

	// NOTE(RJ) interner expects a stable arena pointer
	build->interner = manny_interner_create(&build->arena);
	build->graph = manny_create();

	if (!build->interner || !build->graph) {
		logger_log_string(LOG_LEVEL_ERROR, "build", LIT("cannot initiate build, could not allocate memory"));
		goto failure;
	}

	String absolute;
	Scratch scratch = begin_scratch();
	if (!root.data || root.size == 0 || !manny_path_absolute(scratch.arena, root, &absolute)) {
		end_scratch(scratch);
		goto failure;
	}

	absolute = manny_path_normalize_separators(absolute);
	build->root.atom = manny_interner_intern(build->interner, absolute);
	end_scratch(scratch);
	if (!manny_path_is_valid(build->root)) goto failure;
	return build;

failure:
	manny_destroy(build->graph);
	manny_interner_destroy(build->interner);
	arena_destroy(&arena);
	return NULL;
}

Manny_Build *manny_build_create(void)
{
	Scratch scratch = begin_scratch();

	Manny_Build *build = NULL;

	String directory;
	if (manny_platform_current_directory(scratch.arena, &directory)) {
		build = manny_build_create_at(directory);
	}
	else {
		logger_log_string(LOG_LEVEL_ERROR, "build", LIT("cannot initiate build, platform api function 'manny_platform_current_directory' failed"));
	}

	end_scratch(scratch);
	return build;
}

void manny_build_destroy(Manny_Build *build)
{
	Arena arena;
	if (!build) return;
	manny_execution_destroy(build->execution);
	manny_destroy(build->graph);
	manny_interner_destroy(build->interner);
	arena = build->arena;
	arena_destroy(&arena);
}

Manny *manny_build_graph(Manny_Build *build)
{
	return build ? build->graph : NULL;
}

const Manny *manny_build_graph_const(const Manny_Build *build)
{
	return build ? build->graph : NULL;
}


static Manny_Node_Result build_task_action(Manny_Node_Context *context, void *user_data);

static Manny_Rebuild_Decision task_rebuild_decision(Manny_Builder *builder, const Manny_Node *node, const Build_Task *task)
{
	const Manny_Path_Array *inputs = &task->inputs;
	const Manny_Path_Array *outputs = &task->outputs;
	u64 oldest_output = UINT64_MAX;
	u64 newest_input = 0;
	u64 primary_output_stamp = 0;
	String oldest_output_path = {0};
	String newest_input_path = {0};

	if (outputs->count == 0) return (Manny_Rebuild_Decision){
		.reason = MANNY_REBUILD_NO_OUTPUTS,
		.rebuild = true,
	};
	for (u32 i = 0; i < outputs->count; ++i) {
		Manny_Platform_File_Info info;
		String path = manny_path_string(builder->build, outputs->items[i]);
		if (!manny_platform_file_info(path, &info)) {
			return (Manny_Rebuild_Decision){
				.reason = MANNY_REBUILD_OUTPUT_MISSING,
				.path = path,
				.rebuild = true,
			};
		}
		if (!info.is_directory && (u64)info.modified_unix_ms < oldest_output) {
			oldest_output = (u64)info.modified_unix_ms;
			oldest_output_path = path;
		}
		if (i == 0 && !info.is_directory) primary_output_stamp = (u64)info.modified_unix_ms;
	}
	for (u32 i = 0; i < inputs->count; ++i) {
		Manny_Platform_File_Info info;
		String path = manny_path_string(builder->build, inputs->items[i]);
		if (!manny_platform_file_info(path, &info)) {
			return (Manny_Rebuild_Decision){
				.reason = MANNY_REBUILD_INPUT_MISSING,
				.path = path,
				.rebuild = true,
			};
		}
		if ((u64)info.modified_unix_ms > newest_input) {
			newest_input = (u64)info.modified_unix_ms;
			newest_input_path = path;
		}
	}

	if (outputs->count > 0) {
		Manny_Path output_path = outputs->items[0];
		String output = manny_path_string(builder->build, output_path);
		Build_Record_Task state_task;
		if (!manny_build_snapshot_get(&builder->record_snapshot, output_path, &state_task)) return (Manny_Rebuild_Decision){ .reason = MANNY_REBUILD_STATE_MISSING, .path = output, .rebuild = true };
		if (state_task.output_stamp != primary_output_stamp) return (Manny_Rebuild_Decision){ .reason = MANNY_REBUILD_STATE_CHANGED, .path = output, .rebuild = true };
		if (memcmp(state_task.fingerprint.bytes, task->fingerprint.bytes, MANNY_FINGERPRINT_SIZE) != 0) return (Manny_Rebuild_Decision){ .reason = MANNY_REBUILD_FINGERPRINT_CHANGED, .path = output, .rebuild = true };
		for (u32 i = 0; i < state_task.dependencies.count; ++i) {
			Manny_Platform_File_Info info;
			String dependency = manny_path_string(builder->build, state_task.dependencies.items[i]);
			if (!dependency.data || !manny_platform_file_info(dependency, &info)) return (Manny_Rebuild_Decision){ .reason = MANNY_REBUILD_DEPENDENCY_MISSING, .path = dependency, .rebuild = true };
			if ((u64)info.modified_unix_ms > newest_input) {
				newest_input = (u64)info.modified_unix_ms;
				newest_input_path = dependency;
			}
		}
	}

	for (u32 i = 0; i < manny_dependency_count(node); ++i) {
		Manny_Node *dependency = manny_dependency(node, i);
		if (!dependency || manny_execution_node_result(builder->build->execution, dependency).changed) {
			return (Manny_Rebuild_Decision){
				.reason = MANNY_REBUILD_DEPENDENCY_CHANGED,
				.dependency = dependency,
				.rebuild = true,
			};
		}
	}
	if (newest_input > oldest_output) return (Manny_Rebuild_Decision){
		.reason = MANNY_REBUILD_INPUT_NEWER,
		.path = newest_input_path,
		.reference = oldest_output_path,
		.rebuild = true,
	};
	return (Manny_Rebuild_Decision){
		.reason = MANNY_REBUILD_UP_TO_DATE,
		.path = newest_input_path,
		.reference = oldest_output_path,
	};
}

static void run_command(Manny_Node_Context *context, Manny_Build *build, const Build_Task *task, Manny_Build_Completion *completion)
{
	Scratch scratch = begin_different_scratch(context->arena);
	String dependency_file = manny_path_string(build, task->dependency_file);
	String execution_directory = manny_path_string(build, task->execution_directory);
	if (task->tracks_dependencies) {
		dy_remove_file(dependency_file);
	}

	manny_platform_run_command(task->execution_command_line, context->arena,
		(Manny_Platform_Process_Options){
			.working_directory = execution_directory,
			.capture_stderr = true,
		}, &completion->process);
	if (task->tracks_dependencies) {
		String contents;
		String_Array dependencies = {0};
		b32 process_succeeded = completion->process.error_code == 0 &&
			completion->process.exit_code == 0;
		completion->dependency_state_valid = process_succeeded &&
			manny_platform_read_entire_file(scratch.arena, dependency_file, &contents) &&
			make_depfile_parse(scratch.arena, contents, &dependencies);
		if (completion->dependency_state_valid && dependencies.count > 0) {
			completion->dependencies.items = arena_push_zero_aligned(context->arena,
				(u64)dependencies.count * sizeof(*completion->dependencies.items),
				_Alignof(Manny_Path));
			if (!completion->dependencies.items) completion->dependency_state_valid = false;
			for (u32 i = 0; completion->dependency_state_valid && i < dependencies.count; ++i) {
				if (!manny_path_resolve(build, task->execution_directory,
					dependencies.items[i], completion->dependencies.items + i)) {
					completion->dependency_state_valid = false;
				}
				else ++completion->dependencies.count;
			}
		}
		dy_remove_file(dependency_file);
	}
	end_scratch(scratch);
}

static Manny_Node_Result build_task_action(Manny_Node_Context *context, void *user_data)
{
	Manny_Builder *builder = context->execution_data;
	Build_Task *task = user_data;
	Manny_Build_Completion *completion;
	b32 succeeded;

	completion = arena_push_zero_aligned(context->arena, sizeof(*completion), _Alignof(Manny_Build_Completion));
	if (!completion || !builder || !task) return (Manny_Node_Result){0};
	completion->build = builder->build;
	completion->node = context->node;
	completion->task = task;
	{
		Profile_Scope scope = profile_scope_begin("incremental checks");
		completion->decision = task_rebuild_decision(builder, context->node, task);
		profile_scope_end(&scope);
	}
	if (completion->decision.rebuild) {
		Profile_Scope scope = profile_scope_begin("task processes");
		run_command(context, builder->build, task, completion);
		profile_scope_end(&scope);
	}
	succeeded = !completion->decision.rebuild ||
		(completion->process.error_code == 0 && completion->process.exit_code == 0);
	return (Manny_Node_Result){
		.output = completion,
		.succeeded = succeeded,
		.changed = succeeded && completion->decision.rebuild && !task->transparent,
	};
}

static void report_explanation(const Manny_Build_Completion *completion)
{
	const Manny_Rebuild_Decision *decision = &completion->decision;
	const char *name = manny_task_name(completion->node);
	switch (decision->reason) {
	case MANNY_REBUILD_UP_TO_DATE:
		logger_log(LOG_LEVEL_INFO, "explain", "%s: inputs are not newer than outputs", name);
		break;
	case MANNY_REBUILD_NO_OUTPUTS:
		logger_log(LOG_LEVEL_INFO, "explain", "%s: rebuilding because no outputs are declared", name);
		break;
	case MANNY_REBUILD_OUTPUT_MISSING:
		logger_log(LOG_LEVEL_INFO, "explain", "%s: rebuilding because output is missing: %s", name, decision->path.data);
		break;
	case MANNY_REBUILD_INPUT_MISSING:
		logger_log(LOG_LEVEL_INFO, "explain", "%s: rebuilding because input is missing: %s", name, decision->path.data);
		break;
	case MANNY_REBUILD_STATE_MISSING:
		logger_log(LOG_LEVEL_INFO, "explain", "%s: rebuilding because dependency state is missing: %s", name, decision->path.data);
		break;
	case MANNY_REBUILD_STATE_CHANGED:
		logger_log(LOG_LEVEL_INFO, "explain", "%s: rebuilding because recorded state does not match the output: %s", name, decision->path.data);
		break;
	case MANNY_REBUILD_FINGERPRINT_CHANGED:
		logger_log(LOG_LEVEL_INFO, "explain", "%s: rebuilding because task configuration changed: %s", name, decision->path.data);
		break;
	case MANNY_REBUILD_DEPENDENCY_MISSING:
		logger_log(LOG_LEVEL_INFO, "explain", "%s: rebuilding because recorded dependency is missing: %s", name, decision->path.data);
		break;
	case MANNY_REBUILD_DEPENDENCY_CHANGED:
		logger_log(LOG_LEVEL_INFO, "explain", "%s: rebuilding because dependency changed: %s", name,
			decision->dependency ? manny_task_name(decision->dependency) : "unknown");
		break;
	case MANNY_REBUILD_INPUT_NEWER:
		logger_log(LOG_LEVEL_INFO, "explain", "%s: rebuilding because %s is newer than %s", name,
			decision->path.data, decision->reference.data);
		break;
	}
}

static void report_completion(const Manny_Build_Completion *completion, u32 completed, u32 total)
{
	const Build_Task *build_task = completion->task;
	String command_line = build_task->command_line;
	b32 succeeded = !completion->decision.rebuild ||
		(completion->process.error_code == 0 && completion->process.exit_code == 0);
	char tag[64];

	if (!completion->decision.rebuild) {
		snprintf(tag, sizeof(tag), "%u/%u up-to-date", completed, total);
		logger_log_at(0, LOG_LEVEL_INFO, tag, "%s", manny_task_name(completion->node));
		logger_log_at(1, LOG_LEVEL_TRACE, "command", "%s", command_line.data);
		return;
	}
	if (completion->process.output.size > 0) {
		if (succeeded) logger_log_string_at(0, LOG_LEVEL_INFO,
			manny_task_name(completion->node), completion->process.output);
		else logger_log_string(LOG_LEVEL_ERROR, manny_task_name(completion->node),
			completion->process.output);
	}
	snprintf(tag, sizeof(tag), "%u/%u %s", completed, total, succeeded ? "succeeded" : "failed");
	logger_log_at(0, succeeded ? LOG_LEVEL_SUCCESS : LOG_LEVEL_ERROR, tag, "%s", manny_task_name(completion->node));
	if (succeeded) {
		logger_log_at(1, LOG_LEVEL_TRACE, "command", "%s", command_line.data);
		logger_log_at(1, LOG_LEVEL_TRACE, "exit-code", "0");
	}
	if (completion->process.error_code != 0) {
		Scratch scratch = begin_scratch();
		logger_log(LOG_LEVEL_ERROR, manny_task_name(completion->node), "%s",
			completion->process.launched ? "process error" : "failed to start process");
		logger_log_at(1, LOG_LEVEL_ERROR, "command", "%s", command_line.data);
		{
			String message;
			if (manny_platform_error_message(completion->process.error_code,
				scratch.arena, &message)) {
				logger_log(LOG_LEVEL_ERROR, "os", "error %u: %s",
					completion->process.error_code, message.data);
			}
			else logger_log(LOG_LEVEL_ERROR, "os", "error %u",
				completion->process.error_code);
		}
		if (build_task->compiler.executable.data) {
			logger_log(LOG_LEVEL_ERROR, "executable", "%s (%s)",
				build_task->compiler.executable.data,
				manny_platform_executable_resolves(build_task->compiler.executable) ?
				"found" : "not found in current directory or PATH");
		}
		else logger_log(LOG_LEVEL_ERROR, "executable", "unable to parse from command");
		String execution_directory = manny_path_string(completion->build, build_task->execution_directory);
		logger_log(LOG_LEVEL_ERROR, "working-directory", "%s", execution_directory.data);
		end_scratch(scratch);
	}
	else if (completion->process.exit_code != 0) {
		logger_log(LOG_LEVEL_ERROR, manny_task_name(completion->node),
			"process exited with code %u", completion->process.exit_code);
		logger_log_at(1, LOG_LEVEL_ERROR, "command", "%s", command_line.data);
	}
}

static void record_task_completion_state(Manny_Builder *builder, const Manny_Build_Completion *completion, b32 succeeded)
{
	if (!completion->decision.rebuild || completion->task->outputs.count == 0 || builder->internal_error) return;
	Manny_Path output_path = completion->task->outputs.items[0];
	String state_path = manny_path_string(builder->build, builder->state_path);

	if (!succeeded || (completion->task->tracks_dependencies && !completion->dependency_state_valid)) {
		if (!manny_build_recorder_append_remove(&builder->record_stream, state_path, output_path)) builder->internal_error = true;
		else builder->state_changed = true;

		if (succeeded && completion->task->tracks_dependencies && !completion->dependency_state_valid) {
			log_warning("could not read compiler dependencies for %s", manny_task_name(completion->node));
		}
		return;
	}
	Manny_Platform_File_Info info;
	String output = manny_path_string(builder->build, output_path);
	u64 output_stamp = manny_platform_file_info(output, &info) && !info.is_directory ? (u64)info.modified_unix_ms : 0;
	Build_Record_Task record = {
		.output = output_path,
		.output_stamp = output_stamp,
		.fingerprint = completion->task->fingerprint,
		.dependencies = completion->dependencies,
	};
	if (!manny_build_recorder_append_set(&builder->record_stream, state_path, record)) builder->internal_error = true;
	else builder->state_changed = true;
}

// TODO(RJ): this is to be removed!
static void build_task_event(Manny_Event event, void *user_data)
{
	Manny_Builder *builder = user_data;

	if (event.type == MANNY_EVENT_COMPLETED && event.node && manny_node_function(event.node) == build_task_action)
	{
		Manny_Build_Completion *completion = event.result.output;
		builder->completed_task_count ++;
		if (!completion) builder->internal_error = true;
		else
		{
			Profile_Scope scope = profile_scope_begin("report completion");
			if (builder->explain) report_explanation(completion);
			report_completion(completion, builder->completed_task_count, builder->task_count);
			profile_scope_end(&scope);

			record_task_completion_state(builder, completion, event.result.succeeded);
		}
	}
	if (builder->event) builder->event(event, builder->event_user_data);
}

static b32 valid_task(const Build_Task *task)
{
	return task && task->command_line.data && task->execution_command_line.data &&
		manny_path_is_valid(task->execution_directory) &&
		(!task->inputs.count || task->inputs.items) &&
		(!task->outputs.count || task->outputs.items) &&
		(!task->include_directories.count || task->include_directories.items);
}

b32 manny_build(Manny_Build *build, Manny_Build_Params options)
{
	Manny_Builder builder = {0};
	Manny_Error execution_error;

	if (!build || !build->graph || options.worker_count == 0) return false;

	manny_execution_destroy(build->execution);
	build->execution = NULL;

	Manny *manny = build->graph;

	builder.build = build;
	builder.manny   = manny;
	builder.event = options.event;
	builder.event_user_data = options.user_data;
	builder.explain = options.explain;

	if (!manny_path_resolve(build, manny_build_root(build), LIT(MANNY_BUILD_STATE_PATH), &builder.state_path)) return false;

	for (u32 i = 0; i < manny_node_count(manny); ++i) {
		Manny_Node *node = manny_node_at(manny, i);
		if (manny_node_function(node) != build_task_action) continue;
		const Build_Task *task = manny_node_user_data(node);
		if (!valid_task(task)) return false;
		++builder.task_count;
		if (task->outputs.count > 0) builder.state_tracking = true;
	}

	builder.state_arena = arena_create(MEGABYTES(64));
	arena_set_name(&builder.state_arena, "build state");

	b32 result = true;

	if (!builder.state_arena.data) {
		result = false;
		goto cleanup;
	}
	if (!manny_build_recorder_init(&builder.record_stream, &builder.state_arena, build)) {
		result = false;
		goto cleanup;
	}

	if (builder.state_tracking) {
		String state_path = manny_path_string(build, builder.state_path);
		Build_Record_Result load_result = manny_build_recorder_load(&builder.record_stream, state_path);
		if (load_result == BUILD_RECORD_ERROR) {
			log_warning("could not load Manny build state");
			result = false;
			goto cleanup;
		}
		else if (load_result == BUILD_RECORD_INVALID) {
			log_warning("ignoring invalid Manny build state");
			manny_build_recorder_clear(&builder.record_stream);
		}
		if (load_result != BUILD_RECORD_OK) {
			if (!manny_build_recorder_compact(&builder.record_stream, state_path)) {
				log_warning("could not prepare Manny build state");
				result = false;
				goto cleanup;
			}
		}
	}

	if (!manny_build_snapshot(&builder.record_stream, &builder.state_arena, &builder.record_snapshot)) {
		result = false;
		goto cleanup;
	}

	execution_error = manny_execution_create(manny, &build->execution);
	if (execution_error != MANNY_OK) {
		log_error("unable to create Manny execution: %s", manny_error_string(execution_error));
		result = false;
		goto cleanup;
	}
	// TODO(RJ): can we get rid of this callback thing and instead just have a loop here that
	// executes until get an event?!
	result = manny_execute(build->execution, (Manny_Exec_Params){
		.worker_count = options.worker_count,
		.user_data = &builder,
		.event = build_task_event,
	});
	if (builder.internal_error) result = false;
	if (result && builder.state_tracking && builder.state_changed) {
		if (!manny_build_recorder_compact(&builder.record_stream, manny_path_string(build, builder.state_path))) {
			log_warning("could not compact Manny build state");
			result = false;
		}
	}

cleanup:
	if (builder.record_stream.initialized) manny_build_recorder_destroy(&builder.record_stream);
	arena_destroy(&builder.state_arena);
	return result;
}

static void fingerprint_u32(blake3_hasher *hasher, u32 value)
{
	u8 bytes[4] = {
		(u8)(value >>  0),
		(u8)(value >>  8),
		(u8)(value >> 16),
		(u8)(value >> 24),
	};
	blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

static void fingerprint_u64(blake3_hasher *hasher, u64 value)
{
	u8 bytes[8] = {
		(u8)(value >>  0),
		(u8)(value >>  8),
		(u8)(value >> 16),
		(u8)(value >> 24),
		(u8)(value >> 32),
		(u8)(value >> 40),
		(u8)(value >> 48),
		(u8)(value >> 56),
	};
	blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

static b32 fingerprint_string(blake3_hasher *hasher, String value)
{
	if (!hasher || (!value.data && value.size) || value.size > SIZE_MAX) return false;
	fingerprint_u64(hasher, value.size);
	if (value.size) blake3_hasher_update(hasher, value.data, (size_t)value.size);
	return true;
}

static b32 fingerprint_paths(const Manny_Build *build, blake3_hasher *hasher, Manny_Path_Array paths)
{
	if (!build || !hasher || (paths.count && !paths.items)) return false;
	fingerprint_u32(hasher, paths.count);
	for (u32 i = 0; i < paths.count; ++i) {
		String path = manny_path_string(build, paths.items[i]);
		if (!path.data || !fingerprint_string(hasher, path)) return false;
	}
	return true;
}

static b32 build_task_fingerprint(const Manny_Build *build, const Build_Task *task, Manny_Fingerprint *result)
{
	static const char domain[] = "manny.task.fingerprint";
	blake3_hasher hasher;
	if (!build || !task || !result) return false;
	blake3_hasher_init(&hasher);
	blake3_hasher_update(&hasher, domain, sizeof(domain) - 1);
	fingerprint_u32(&hasher, 1);
	if (!fingerprint_string(&hasher, task->command_line)) return false;
	if (!fingerprint_string(&hasher, manny_path_string(build, task->execution_directory))) return false;
	if (!fingerprint_paths(build, &hasher, task->inputs)) return false;
	if (!fingerprint_paths(build, &hasher, task->outputs)) return false;
	if (!fingerprint_paths(build, &hasher, task->include_directories)) return false;
	fingerprint_u32(&hasher, task->transparent ? 1 : 0);
	blake3_hasher_finalize(&hasher, result->bytes, sizeof(result->bytes));
	return true;
}

static b32 copy_optional_string(Manny *manny, String source, String *result)
{
	if (!source.data) return source.size == 0;
	*result = manny_copy_string(manny, source);
	return result->data != NULL;
}

static b32 resolve_task_paths(Manny_Build *build, Manny_Path directory, String_Array source, Manny_Path_Array *result)
{
	Manny *manny = build ? build->graph : NULL;
	*result = (Manny_Path_Array){0};
	if (source.count == 0) return true;
	result->items = manny_allocate(manny, (u64)source.count * sizeof(*result->items), _Alignof(Manny_Path));
	if (!result->items) return false;
	for (u32 i = 0; i < source.count; ++i) {
		if (!manny_path_resolve(build, directory, source.items[i], result->items + i)) return false;
		++result->count;
	}
	return true;
}

static Build_Task *create_build_task(Manny_Build *build, Manny_Task_Desc desc)
{
	Manny *manny = build ? build->graph : NULL;
	Build_Task *task = manny_allocate(manny, sizeof(*task), _Alignof(Build_Task));
	Scratch scratch;
	Compiler_Command compiler;
	b32 valid = false;
	if (!task || !desc.command_line.data) return NULL;
	if (desc.working_directory.data && desc.working_directory.size == 0) return NULL;
	task->command_line = manny_copy_string(manny, desc.command_line);
	task->transparent = desc.transparent;
	if (!task->command_line.data) return NULL;

	scratch = begin_scratch();
	task->execution_directory = manny_build_root(build);
	if (desc.working_directory.data && !manny_path_resolve(build, task->execution_directory, desc.working_directory, &task->execution_directory)) goto done;
	if (!resolve_task_paths(build, task->execution_directory, desc.inputs, &task->inputs) ||
		!resolve_task_paths(build, task->execution_directory, desc.outputs, &task->outputs) ||
		!resolve_task_paths(build, task->execution_directory, desc.include_directories, &task->include_directories)) goto done;
	if (!compiler_command_parse(scratch.arena, task->command_line, &compiler)) {
		goto done;
	}
	task->compiler = compiler;
	task->compiler.executable = (String){0};
	if (!copy_optional_string(manny, compiler.executable, &task->compiler.executable)) {
		goto done;
	}
	task->execution_command_line = task->command_line;
	task->tracks_dependencies = task->outputs.count > 0 && task->compiler.can_add_make_dependencies;
	if (task->tracks_dependencies) {
		String augmented;
		void *start = arena_top(scratch.arena);
		arena_append_str(scratch.arena, manny_path_string(build, task->outputs.items[0]));
		arena_append_text(scratch.arena, ".d.tmp");
		String dependency_file = arena_string_from(scratch.arena, start);
		if (!manny_path_resolve(build, task->execution_directory, dependency_file, &task->dependency_file) ||
			!compiler_command_add_dependencies(scratch.arena, &task->compiler,
				task->command_line, manny_path_string(build, task->dependency_file), &augmented)) goto done;
		task->execution_command_line = manny_copy_string(manny, augmented);
		if (!task->execution_command_line.data) goto done;
	}
	if (!build_task_fingerprint(build, task, &task->fingerprint)) goto done;
	valid = true;

done:
	end_scratch(scratch);
	return valid ? task : NULL;
}

Manny_Error manny_add_task(Manny_Build *build, Manny_Task_Desc desc, Manny_Node **node_out)
{
	Manny *manny = build ? build->graph : NULL;
	Build_Task *task;
	if (!manny || !desc.name.data || !node_out) return MANNY_ERROR_INVALID_TASK;
	if (manny_is_sealed(manny)) return MANNY_ERROR_GRAPH_SEALED;
	task = create_build_task(build, desc);
	if (!task) return MANNY_ERROR_OUT_OF_MEMORY;
	return manny_add_node(manny, (Manny_Node_Desc){
		.name = desc.name,
		.function = build_task_action,
		.user_data = task,
	}, node_out);
}

// TODO(RJ): remove this entirely, only tests use this thing for whatever reason!
Manny_Error manny_set_task(Manny_Build *build, Manny_Node *node, Manny_Task_Desc task)
{
	Manny *manny = build ? build->graph : NULL;
	Build_Task *copy;
	Manny_Error result;
	if (!manny || !node) return MANNY_ERROR_INVALID_TASK;
	if (manny_is_sealed(manny)) return MANNY_ERROR_GRAPH_SEALED;
	for (u32 i = 0; i < manny_node_count(manny); ++i) {
		if (manny_node_at(manny, i) == node) goto found;
	}
	return MANNY_ERROR_INVALID_TASK;

found:
	copy = create_build_task(build, task);
	if (!copy) return MANNY_ERROR_OUT_OF_MEMORY;
	result = manny_set_node(manny, node, (Manny_Node_Desc){
		.name = task.name,
		.function = build_task_action,
		.user_data = copy,
	});
	return result;
}

u32 manny_task_count(const Manny_Build *build)
{
	return manny_node_count(manny_build_graph_const(build));
}

const char *manny_task_name(const Manny_Node *node)
{
	return manny_node_name(node);
}

Manny_Node_Status manny_task_state(const Manny_Build *build, const Manny_Node *node)
{
	return build && build->execution ? manny_execution_node_state(build->execution, node) : MANNY_NODE_PENDING;
}

