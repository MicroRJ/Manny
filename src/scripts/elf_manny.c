#include "elf_manny.h"

#include "manny_build.h"
#include "logger.h"
#include "platform_adapter.h"
#include "profiler.h"
#include "script.h"

#include <stdio.h>
#include <string.h>

typedef struct Elf_Build_Options
{
	u32 worker_count;
	i32 verbosity;
	b32 has_worker_count;
	b32 has_verbosity;
}
Elf_Build_Options;

typedef struct Elf_Build
{
	Manny_Build *build;
	Elf_Build_Options options;
	char error[256];
}
Elf_Build;

typedef struct Ref_List
{
	Arena   *arena;
	elf_Ref *items;
	u32      count;
}
Ref_List;

static u32 ref_list_find(Ref_List *list, elf_State *state, elf_i32 value)
{
	elf_i32 absolute = elf_abs_index(state, value);
	for (u32 i = 0; i < list->count; ++i)
	{
		if (elf_push_ref(state, list->items[i]))
		{
			b32 equal = elf_equal(state, absolute, -1);
			elf_pop(state, 1);
			if (equal)
			{
				return i;
			}
		}
	}
	return UINT32_MAX;
}

static b32 ref_list_add(Ref_List *list, elf_State *state, elf_i32 value)
{
	b32 added = ref_list_find(list, state, value) != UINT32_MAX;
	if (!added)
	{
		elf_Ref reference = elf_create_ref(state, value);
		if (reference != ELF_NO_REF)
		{
			elf_Ref *item = arena_push_aligned(list->arena, sizeof(*item), _Alignof(elf_Ref));
			if (item)
			{
				if (!list->items)
				{
					list->items = item;
				}
				*item = reference;
				++list->count;
				added = true;
			}
			else
			{
				elf_release_ref(state, reference);
			}
		}
	}
	return added;
}

static String stack_string(elf_State *state, elf_Index index)
{
	elf_StrSlice value = {0};
	String result = {0};
	if (elf_to_str(state, index, &value))
	{
		result = string_from_data(value.data, value.size);
	}
	return result;
}

static b32 stack_integer_field(elf_State *state, elf_i32 table, const char *field, elf_Int *value, b32 *present)
{
	b32 result = elf_get_field(state, table, field);
	if (result)
	{
		*present = !elf_is_nil(state, -1);
		result = !*present || elf_to_int(state, -1, value);
		elf_pop(state, 1);
	}
	return result;
}

static String copy_stack_string(Arena *arena, elf_State *state, elf_i32 index)
{
	String string = stack_string(state, index);
	return string.data ? str_push_copy(arena, string) : (String){0};
}

static b32 copy_string_array_field(elf_State *state, Arena *arena, elf_Index table, const char *field_name, String task_name, String_Array *result, char *error, size_t error_size)
{
	elf_Index checkpoint = elf_get_top(state);
	b32 success = elf_get_field(state, table, field_name);
	String_Array strings = {0};
	if (!success)
	{
		snprintf(error, error_size, "unable to read %s for '%s'", field_name, task_name.data);
	}
	else if (!elf_is_nil(state, -1))
	{
		elf_Index array = elf_abs_index(state, -1);
		if (elf_type(state, array) != ELF_VALUE_TYPE_ARRAY)
		{
			snprintf(error, error_size, "%s for '%s' must be an array", field_name, task_name.data);
			success = false;
		}
		else
		{
			elf_u32 count = 0;
			elf_length(state, array, &count);
			String *items = NULL;
			if (count)
			{
				items = arena_push_zero_aligned(arena, count * sizeof(*items), _Alignof(String));
				if (!items)
				{
					snprintf(error, error_size, "unable to copy %s for '%s'", field_name, task_name.data);
					success = false;
				}
			}

			for (elf_u32 i = 0; i < count && success; ++i)
			{
				elf_get_index(state, array, i);
				elf_StrSlice slice = {0};
				if (!elf_to_str(state, -1, &slice))
				{
					snprintf(error, error_size, "%s for '%s' must contain strings", field_name, task_name.data);
					success = false;
				}
				else
				{
					String string = string_from_data(slice.data, slice.size);
					items[i] = str_push_copy(arena, string);
					if (!items[i].data)
					{
						snprintf(error, error_size, "unable to copy %s for '%s'", field_name, task_name.data);
						success = false;
					}
				}
				elf_pop(state, 1);
			}
			if (success)
			{
				strings = (String_Array){ .items = items, .count = count };
			}
		}
	}
	elf_set_top(state, checkpoint);
	*result = strings;
	return success;
}

static b32 set_function(elf_State *state, elf_i32 table, const char *name, elf_Function function)
{
	elf_push_fun(state, function);
	return elf_set_field(state, table, name) == ELF_ERROR_NONE;
}

static b32 load_build(Script *script, elf_i32 root, Elf_Build *result)
{
	ASSERT(script);
	ASSERT(result);

	elf_State *state = script->state;
	elf_i32 checkpoint = elf_get_top(state);
	root = elf_abs_index(state, root);
	Scratch scratch = begin_scratch();
	Ref_List task_tables = { .arena = scratch.arena };
	Elf_Build_Options options = {0};
	String build_root = {0};
	Manny_Build *build = NULL;
	b32 success = false;
	*result = (Elf_Build){0};

	elf_get_field(state, root, "root");
	if (!elf_is_nil(state, -1))
	{
		build_root = copy_stack_string(scratch.arena, state, -1);
		if (!build_root.data || build_root.size == 0)
		{
			snprintf(result->error, sizeof(result->error), "'root' must be a non-empty string");
			goto cleanup;
		}
	}
	elf_pop(state, 1);

	elf_get_field(state, root, "options");
	if (!elf_is_nil(state, -1))
	{
		elf_i32 options_table = elf_abs_index(state, -1);
		if (elf_type(state, options_table) != ELF_VALUE_TYPE_TABLE)
		{
			snprintf(result->error, sizeof(result->error), "'options' must be a table");
			goto cleanup;
		}
		elf_Int integer = 0;
		b32 present = false;
		if (!stack_integer_field(state, options_table, "workers", &integer, &present) || (present && (integer < 1 || (u64)integer > UINT32_MAX)))
		{
			snprintf(result->error, sizeof(result->error), "options.workers must be a positive integer");
			goto cleanup;
		}
		if (present)
		{
			options.worker_count = (u32)integer;
			options.has_worker_count = true;
		}
		if (!stack_integer_field(state, options_table, "verbosity", &integer, &present) || (present && (integer < 0 || (u64)integer > INT32_MAX)))
		{
			snprintf(result->error, sizeof(result->error), "options.verbosity must be a non-negative integer");
			goto cleanup;
		}
		if (present)
		{
			options.verbosity = (i32)integer;
			options.has_verbosity = true;
		}
	}
	elf_pop(state, 1);

	elf_get_field(state, root, "targets");
	elf_i32 targets = elf_abs_index(state, -1);
	elf_u32 target_count = 0;
	if (elf_type(state, targets) != ELF_VALUE_TYPE_ARRAY)
	{
		snprintf(result->error, sizeof(result->error), "manny.build requires a 'targets' array");
		goto cleanup;
	}
	elf_length(state, targets, &target_count);
	for (elf_u32 i = 0; i < target_count; ++i)
	{
		elf_get_index(state, targets, i);
		if (elf_type(state, -1) != ELF_VALUE_TYPE_TABLE)
		{
			snprintf(result->error, sizeof(result->error), "target %u must be a task table", i);
			goto cleanup;
		}
		if (!ref_list_add(&task_tables, state, -1))
		{
			snprintf(result->error, sizeof(result->error), "unable to retain target %u", i);
			goto cleanup;
		}
		elf_pop(state, 1);
	}
	elf_pop(state, 1);

	for (u32 i = 0; i < task_tables.count; ++i)
	{
		elf_i32 task_checkpoint = elf_get_top(state);
		if (!elf_push_ref(state, task_tables.items[i]))
		{
			snprintf(result->error, sizeof(result->error), "unable to read task %u", i);
			goto cleanup;
		}
		elf_i32 task = elf_abs_index(state, -1);
		elf_get_field(state, task, "dependencies");
		if (!elf_is_nil(state, -1))
		{
			elf_i32 dependencies = elf_abs_index(state, -1);
			if (elf_type(state, dependencies) != ELF_VALUE_TYPE_ARRAY)
			{
				snprintf(result->error, sizeof(result->error), "dependencies for task %u must be an array", i);
				goto cleanup;
			}
			elf_u32 dependency_count = 0;
			elf_length(state, dependencies, &dependency_count);
			for (elf_u32 dependency = 0; dependency < dependency_count; ++dependency)
			{
				elf_get_index(state, dependencies, dependency);
				if (elf_type(state, -1) != ELF_VALUE_TYPE_TABLE)
				{
					snprintf(result->error, sizeof(result->error), "dependencies for task %u must contain task tables", i);
					goto cleanup;
				}
				if (!ref_list_add(&task_tables, state, -1))
				{
					snprintf(result->error, sizeof(result->error), "unable to retain dependency for task %u", i);
					goto cleanup;
				}
				elf_pop(state, 1);
			}
		}
		elf_set_top(state, task_checkpoint);
	}

	build = build_root.data ? manny_build_create_at(build_root) : manny_build_create();
	if (!build)
	{
		if (build_root.data)
		{
			snprintf(result->error, sizeof(result->error), "unable to create build at root '%s'", build_root.data);
		}
		else
		{
			snprintf(result->error, sizeof(result->error), "unable to create build");
		}
		goto cleanup;
	}

	for (u32 i = 0; i < task_tables.count; ++i)
	{
		elf_i32 task_checkpoint = elf_get_top(state);
		if (!elf_push_ref(state, task_tables.items[i]))
		{
			snprintf(result->error, sizeof(result->error), "unable to read task %u", i);
			goto cleanup;
		}
		elf_i32 description = elf_abs_index(state, -1);

		elf_get_field(state, description, "name");
		String name = copy_stack_string(scratch.arena, state, -1);
		elf_pop(state, 1);
		elf_get_field(state, description, "exec");
		String executable = copy_stack_string(scratch.arena, state, -1);
		elf_pop(state, 1);
		if (!name.data || !executable.data)
		{
			snprintf(result->error, sizeof(result->error), "task %u requires string fields 'name' and 'exec'", i);
			goto cleanup;
		}

		String_Array arguments = {0};
		if (!copy_string_array_field(state, scratch.arena, description, "args", name, &arguments, result->error, sizeof(result->error)))
		{
			goto cleanup;
		}

		String working_directory = {0};
		elf_get_field(state, description, "working_directory");
		if (!elf_is_nil(state, -1))
		{
			working_directory = copy_stack_string(scratch.arena, state, -1);
			if (!working_directory.data || working_directory.size == 0)
			{
				snprintf(result->error, sizeof(result->error), "working_directory for '%s' must be a non-empty string", name.data);
				goto cleanup;
			}
		}
		elf_pop(state, 1);

		elf_Int transparent = 0;
		b32 has_transparent = false;
		if (!stack_integer_field(state, description, "transparent", &transparent, &has_transparent))
		{
			snprintf(result->error, sizeof(result->error), "transparent for '%s' must be a boolean", name.data);
			goto cleanup;
		}
		b32 is_transparent = has_transparent && transparent != 0;

		String_Array inputs = {0};
		if (!copy_string_array_field(state, scratch.arena, description, "inputs", name, &inputs, result->error, sizeof(result->error)))
		{
			goto cleanup;
		}
		String_Array outputs = {0};
		if (!copy_string_array_field(state, scratch.arena, description, "outputs", name, &outputs, result->error, sizeof(result->error)))
		{
			goto cleanup;
		}
		String_Array include_directories = {0};
		if (!copy_string_array_field(state, scratch.arena, description, "include_dirs", name, &include_directories, result->error, sizeof(result->error)))
		{
			goto cleanup;
		}

		Manny_Task_Desc task = {
			.name = name,
			.executable = executable,
			.arguments = arguments,
			.working_directory = working_directory,
			.transparent = is_transparent,
			.inputs = inputs,
			.outputs = outputs,
			.include_directories = include_directories,
		};
		Manny_Node *node = NULL;
		Manny_Error manny_error = manny_add_task(build, task, &node);
		if (manny_error != MANNY_OK)
		{
			snprintf(result->error, sizeof(result->error), "unable to add task '%s': %s", name.data, manny_error_string(manny_error));
			goto cleanup;
		}
		elf_set_top(state, task_checkpoint);
	}

	Manny *graph = manny_build_graph(build);
	for (u32 i = 0; i < task_tables.count; ++i)
	{
		elf_i32 task_checkpoint = elf_get_top(state);
		if (!elf_push_ref(state, task_tables.items[i]))
		{
			snprintf(result->error, sizeof(result->error), "unable to read task %u", i);
			goto cleanup;
		}
		elf_i32 description = elf_abs_index(state, -1);
		elf_get_field(state, description, "dependencies");
		if (!elf_is_nil(state, -1))
		{
			elf_i32 dependencies = elf_abs_index(state, -1);
			elf_u32 dependency_count = 0;
			elf_length(state, dependencies, &dependency_count);
			for (elf_u32 dependency = 0; dependency < dependency_count; ++dependency)
			{
				elf_get_index(state, dependencies, dependency);
				u32 resolved = ref_list_find(&task_tables, state, -1);
				elf_pop(state, 1);
				if (resolved == UINT32_MAX)
				{
					snprintf(result->error, sizeof(result->error), "unable to resolve dependency for task %u", i);
					goto cleanup;
				}
				Manny_Node *node = manny_node_at(graph, i);
				Manny_Node *dependency_node = manny_node_at(graph, resolved);
				Manny_Error manny_error = manny_add_dependency(graph, node, dependency_node);
				if (manny_error != MANNY_OK)
				{
					snprintf(result->error, sizeof(result->error), "unable to add dependency to '%s': %s", manny_task_name(node), manny_error_string(manny_error));
					goto cleanup;
				}
			}
		}
		elf_set_top(state, task_checkpoint);
	}
	success = true;

cleanup:
	elf_set_top(state, checkpoint);
	for (u32 i = 0; i < task_tables.count; ++i)
	{
		elf_release_ref(state, task_tables.items[i]);
	}
	end_scratch(scratch);
	if (success)
	{
		result->build = build;
		result->options = options;
	}
	else if (build)
	{
		manny_build_destroy(build);
	}
	return success;
}

ELF_FUNCTION(l_manny_build)
{
	(void)nrets;
	if (nargs != 2 || elf_type(S, 1) != ELF_VALUE_TYPE_TABLE)
	{
		elf_error(S, "manny.build expects one build description table");
		return 0;
	}

	Script *script = elf_get_user_data(S);
	Elf_Build build = {0};
	if (!load_build(script, 1, &build))
	{
		script_set_error(script, "%s", build.error);
		script->failed = true;
		elf_push_int(S, false);
		return 1;
	}

	u32 worker_count = build.options.has_worker_count ? build.options.worker_count : 4;
	i32 verbosity = build.options.has_verbosity ? build.options.verbosity : 0;
	if (script->invocation_options.has_worker_count)
	{
		worker_count = script->invocation_options.worker_count;
	}
	if (script->invocation_options.has_verbosity)
	{
		verbosity = script->invocation_options.verbosity;
	}
	logger_set_verbosity(verbosity);
	Profile_Scope scope = profile_scope_begin("builder");
	b32 succeeded = manny_build(build.build, (Manny_Build_Params){
		.worker_count = worker_count,
		.explain = script->invocation_options.explain,
	});
	profile_scope_end(&scope);
	manny_build_destroy(build.build);
	if (!succeeded)
	{
		script_set_error(script, "build failed");
		script->failed = true;
	}
	elf_push_int(S, succeeded);
	return 1;
}

ELF_FUNCTION(l_manny_load)
{
	const char *name = NULL;
	if (nargs != 2 || !elf_to_cstr(S, 1, &name))
	{
		elf_error(S, "manny.load expects one module name");
		return 0;
	}

	const char *filename = strcmp(name, "c") == 0 ? "c.elf" : NULL;
	if (!filename)
	{
		elf_error(S, "unknown Manny module");
		return 0;
	}

	Scratch scratch = begin_scratch();
	String executable = {0};
	if (!manny_platform_executable_path(scratch.arena, &executable))
	{
		end_scratch(scratch);
		elf_error(S, "could not locate the Manny executable");
		return 0;
	}

	u64 directory_size = executable.size;
	while (directory_size && executable.data[directory_size - 1] != '/' && executable.data[directory_size - 1] != '\\')
	{
		--directory_size;
	}
	void *start = arena_top(scratch.arena);
	char *directory = arena_append_str(scratch.arena, string_slice(executable, 0, directory_size));
	char *module = directory ? arena_append_text(scratch.arena, filename) : NULL;
	if (!module)
	{
		end_scratch(scratch);
		elf_error(S, "could not allocate the Manny module path");
		return 0;
	}
	String path = arena_string_from(scratch.arena, start);
	arena_finalize_string(scratch.arena, path);
	if (!elf_push_code_file(S, path.data))
	{
		elf_pop(S, 1);
		void *message_start = arena_top(scratch.arena);
		arena_appendf(scratch.arena, "Manny module '%s' was not found beside the executable: %s", name, path.data);
		String message = arena_string_from(scratch.arena, message_start);
		arena_finalize_string(scratch.arena, message);
		elf_error(S, message.data);
		end_scratch(scratch);
		return 0;
	}
	end_scratch(scratch);
	elf_push_value(S, 0);
	return elf_tail_call(S, 1, nrets);
}

elf_b32 elf_manny_register(elf_State *state)
{
	elf_i32 checkpoint = elf_get_top(state);
	elf_new_table(state);
	elf_i32 manny = elf_abs_index(state, -1);
	b32 success = set_function(state, manny, "build", l_manny_build);
	if (success)
	{
		success = set_function(state, manny, "load", l_manny_load);
	}
	if (success)
	{
		elf_push_cstr(state, MANNY_VERSION);
		success = elf_set_field(state, manny, "version") == ELF_ERROR_NONE;
	}
	if (success)
	{
		success = elf_set_global(state, "manny") == ELF_ERROR_NONE;
	}
	if (!success)
	{
		elf_set_top(state, checkpoint);
	}
	return success;
}
