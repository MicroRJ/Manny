#include "script.h"

#include "scripts/elf_manny.h"
#include "platform_adapter.h"

#include <stdarg.h>

void script_set_error(Script *script, const char *format, ...)
{
	void *start = arena_top(script->arena);
	va_list arguments;
	va_start(arguments, format);
	arena_appendfv(script->arena, format, arguments);
	va_end(arguments);
	script->error = arena_string_from(script->arena, start);
	arena_finalize_string(script->arena, script->error);
}

static b32 load_script_source(Script *script, String path, String source)
{
	script->state = elf_create_state();
	b32 success = script->state != NULL;
	if (!success)
	{
		script_set_error(script, "unable to create elf state");
	}
	if (success)
	{
		elf_set_user_data(script->state, script);
		success = elf_manny_register(script->state);
		if (!success)
		{
			script_set_error(script, "unable to register Manny script libraries");
		}
	}
	if (success)
	{
		success = elf_push_code_source(script->state, path.data, (elf_StrSlice){ source.data, (elf_u32)source.size }) == ELF_ERROR_NONE;
		if (!success)
		{
			script_set_error(script, "unable to load '%s'", path.data);
		}
	}
	if (success)
	{
		elf_push_nil(script->state);
		elf_call(script->state, 1, 1);
		if (script->failed)
		{
			elf_pop(script->state, 1);
			success = false;
		}
		else if (elf_type(script->state, -1) != ELF_VALUE_TYPE_TABLE)
		{
			elf_pop(script->state, 1);
			script_set_error(script, "script must return a table");
			success = false;
		}
		else
		{
			script->exports = elf_create_ref(script->state, -1);
			elf_pop(script->state, 1);
			if (script->exports == ELF_NO_REF)
			{
				script_set_error(script, "unable to retain script exports");
				success = false;
			}
		}
	}
	if (success)
	{
		elf_i32 checkpoint = elf_get_top(script->state);
		success = elf_push_ref(script->state, script->exports);
		if (!success)
		{
			script_set_error(script, "unable to read script exports");
		}
		else
		{
			elf_i32 exports = elf_abs_index(script->state, -1);
			elf_u32 cursor = 0;
			u32 function_count = 0;
			while (elf_next(script->state, exports, &cursor))
			{
				elf_StrSlice name = {0};
				if (elf_to_str(script->state, -2, &name) && elf_is_callable(script->state, -1))
				{
					++function_count;
				}
				elf_pop(script->state, 2);
			}

			String *functions = NULL;
			if (function_count)
			{
				functions = arena_push_zero_aligned(script->arena, function_count * sizeof(*functions), _Alignof(String));
				if (!functions)
				{
					script_set_error(script, "unable to retain script function names");
					success = false;
				}
			}

			cursor = 0;
			u32 function_index = 0;
			while (elf_next(script->state, exports, &cursor))
			{
				elf_StrSlice slice = {0};
				if (elf_to_str(script->state, -2, &slice) && elf_is_callable(script->state, -1))
				{
					if (success)
					{
						String name = string_from_data(slice.data, slice.size);
						functions[function_index] = str_push_copy(script->arena, name);
						if (!functions[function_index].data)
						{
							script_set_error(script, "unable to retain script function names");
							success = false;
						}
					}
					++function_index;
				}
				elf_pop(script->state, 2);
			}
			if (success)
			{
				script->functions = (String_Array){ .items = functions, .count = function_count };
			}
		}
		elf_set_top(script->state, checkpoint);
	}
	return success;
}

Script *script_load(Arena *arena, String path)
{
	Script *script = arena_push_zero_aligned(arena, sizeof(*script), _Alignof(Script));
	if (script)
	{
		script->arena = arena;
		String source = {0};
		if (!manny_platform_read_entire_file(script->arena, path, &source))
		{
			script_set_error(script, "unable to read '%s'", path.data);
		}
		else if (source.size > UINT32_MAX)
		{
			script_set_error(script, "script is too large: '%s'", path.data);
		}
		else
		{
			script->loaded = load_script_source(script, path, source);
		}
	}
	return script;
}

void script_destroy(Script *script)
{
	if (script)
	{
		if (script->state)
		{
			if (script->exports != ELF_NO_REF)
			{
				elf_release_ref(script->state, script->exports);
			}
			elf_destroy_state(script->state);
		}
		script->exports = ELF_NO_REF;
		script->state = NULL;
		script->loaded = false;
	}
}

b32 script_is_loaded(Script *script)
{
	return script && script->loaded;
}

String script_error(Script *script)
{
	return script ? script->error : (String){0};
}

String_Array script_functions(Script *script)
{
	return script ? script->functions : (String_Array){0};
}

b32 script_has_function(Script *script, String name)
{
	b32 result = false;
	if (script)
	{
		for (u32 i = 0; i < script->functions.count && !result; ++i)
		{
			result = string_equal(script->functions.items[i], name);
		}
	}
	return result;
}

void script_set_invocation_options(Script *script, Script_Invocation_Options options)
{
	if (script)
	{
		script->invocation_options = options;
	}
}

b32 script_invoke(Script *script, String name)
{
	b32 result = false;
	if (script_is_loaded(script))
	{
		if (!script_has_function(script, name))
		{
			script_set_error(script, "script has no function '%.*s'", (int)name.size, name.data);
		}
		else
		{
			script->failed = false;
			Scratch scratch = begin_different_scratch(script->arena);
			String terminated_name = str_push_copy(scratch.arena, name);
			if (!terminated_name.data)
			{
				script_set_error(script, "unable to invoke '%.*s'", (int)name.size, name.data);
			}
			else
			{
				elf_i32 checkpoint = elf_get_top(script->state);
				if (elf_push_ref(script->state, script->exports))
				{
					elf_i32 exports = elf_abs_index(script->state, -1);
					if (elf_get_field(script->state, exports, terminated_name.data) && elf_is_callable(script->state, -1))
					{
						elf_push_nil(script->state);
						elf_call(script->state, 1, 0);
						result = !script->failed;
					}
				}
				elf_set_top(script->state, checkpoint);
			}
			end_scratch(scratch);
		}
	}
	return result;
}
