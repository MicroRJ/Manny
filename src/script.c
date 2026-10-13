#include "script.h"

#include "scripts/elf_adapter.h"
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
			script->loaded = elf_script_load(script, path, source);
		}
	}
	return script;
}

void script_destroy(Script *script)
{
	if (script)
	{
		if (script->elf)
		{
			elf_script_destroy(script);
		}
		script->elf = NULL;
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
			result = elf_script_invoke(script, terminated_name) && !script->failed;
			end_scratch(scratch);
		}
	}
	return result;
}
