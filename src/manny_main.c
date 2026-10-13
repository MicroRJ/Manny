
#include "manny_build.h"
#include "logger.h"
#include "platform_adapter.h"
#include "profiler.h"
#include "script.h"
#include "elf.h"

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>

static int run_script(String path, String function_name, Script_Invocation_Options invocation_options)
{
	Manny_Platform_File_Info build_file;
	if (!manny_platform_file_info(path, &build_file))
	{
		Scratch scratch = begin_scratch();
		String working_directory;
		if (manny_platform_current_directory(scratch.arena, &working_directory)) log_error("%s: file not found (working directory: %s)", path.data, working_directory.data);
		else log_error("%s: file not found", path.data);
		end_scratch(scratch);
		return 1;
	}

	Scratch scratch = begin_scratch();
	Profile_Scope load_scope = profile_scope_begin("load build script");
	Script *script = script_load(scratch.arena, path);
	profile_scope_end(&load_scope);
	if (!script_is_loaded(script)) {
		log_error("%s: %s", path.data, script_error(script).data);
		script_destroy(script);
		end_scratch(scratch);
		return 1;
	}
	script_set_invocation_options(script, invocation_options);

	int exit_code = 1;
	if (script_has_function(script, function_name)) {
		exit_code = script_invoke(script, function_name) ? 0 : 1;
		if (exit_code) log_error("%s: %s", path.data, script_error(script).data);
	}
	else
	{
		String_Array functions = script_functions(script);
		if (functions.count)
		{
			void *start = arena_top(scratch.arena);
			for (u32 i = 0; i < functions.count; ++i) {
				if (i) arena_append_text(scratch.arena, ", ");
				arena_append_str(scratch.arena, functions.items[i]);
			}
			String available = arena_string_from(scratch.arena, start);
			arena_finalize_string(scratch.arena, available);
			log_error("script has no function '%.*s' (available: %s)", (int)function_name.size, function_name.data, available.data);
		}
		else log_error("script has no function '%.*s'", (int)function_name.size, function_name.data);
	}

	script_destroy(script);
	end_scratch(scratch);
	return exit_code;
}

int main(int argument_count, char **arguments)
{
   Script_Invocation_Options invocation_options = {0};
   String build_path = LIT("build.elf");
   String function_name = LIT("build");
   b32 has_build_path = false;
   b32 has_function = false;
   b32 quiet = false;
   b32 profile = false;
   b32 profile_threads = false;
   int argument_index;

   logger_init();

   for (argument_index = 1; argument_index < argument_count; ++argument_index)
   {
      if (strcmp(arguments[argument_index], "--verbose") == 0)
      {
         invocation_options.verbosity = 1;
         invocation_options.has_verbosity = true;
         if (argument_index + 1 < argument_count)
         {
            char *end;
            unsigned long parsed = strtoul(arguments[argument_index + 1], &end, 10);
            if (*end == 0)
            {
               if (parsed > INT32_MAX)
               {
                  log_error("invalid verbosity level: %s", arguments[argument_index + 1]);
                  return 2;
               }
               invocation_options.verbosity = (i32)parsed;
               ++argument_index;
            }
         }
      }
      else if (strcmp(arguments[argument_index], "-q") == 0 ||
               strcmp(arguments[argument_index], "--quiet") == 0)
      {
         quiet = true;
      }
      else if (strcmp(arguments[argument_index], "--workers") == 0 && argument_index + 1 < argument_count)
      {
         char *end;
         unsigned long parsed = strtoul(arguments[++argument_index], &end, 10);
         if (*end != 0 || parsed == 0 || parsed > UINT32_MAX)
         {
            log_error("invalid worker count: %s", arguments[argument_index]);
            return 2;
         }
         invocation_options.worker_count = (u32)parsed;
         invocation_options.has_worker_count = true;
      }
      else if (strcmp(arguments[argument_index], "--profile") == 0)
      {
         profile = true;
      }
      else if (strcmp(arguments[argument_index], "--profile-threads") == 0)
      {
         profile = true;
         profile_threads = true;
      }
      else if (strcmp(arguments[argument_index], "--explain") == 0)
      {
         invocation_options.explain = true;
      }
      else if (strcmp(arguments[argument_index], "--version") == 0)
      {
         printf("manny %s\n", MANNY_VERSION);
			printf("elf %s\n", elf_version());
         return 0;
      }
      else if (arguments[argument_index][0] != '-')
      {
         String argument = string_from_cstring(arguments[argument_index]);
         if (string_ends_with_insensitive(argument, LIT(".elf"))) {
            if (has_build_path) {
               log_error("multiple build files specified: %s", argument.data);
               return 2;
            }
            build_path = argument;
            has_build_path = true;
         }
         else if (!has_function) {
            function_name = argument;
            has_function = true;
         }
         else {
            log_error("unexpected positional argument: %s", argument.data);
            return 2;
         }
      }
      else
      {
         log_error("usage: manny [build-file] [function] [-q | --quiet] [--explain] [--verbose [N]] [--workers N] [--profile | --profile-threads]\n" "       manny --version");
         return 2;
      }
   }
   logger_set_quiet(quiet);
   profiler_set_enabled(profile);
   profiler_reset();
   {
      int result;
      Profile_Scope scope = profile_scope_begin("build");
      result = run_script(build_path, function_name, invocation_options);
      profile_scope_end(&scope);
      profiler_print(profile_threads);
      return result;
   }
}
