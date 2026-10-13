#ifndef SCRIPT_H
#define SCRIPT_H

#include "manny_build.h"

typedef struct Script_Invocation_Options
{
	u32 worker_count;
	i32 verbosity;
	b32 explain;
	b32 has_worker_count;
	b32 has_verbosity;
}
Script_Invocation_Options;

typedef struct Elf_Script Elf_Script;
typedef struct Script Script;
struct Script
{
	Arena *arena;
	Elf_Script *elf;
	String_Array functions;
	String error;
	Script_Invocation_Options invocation_options;
	b32 loaded;
	b32 failed;
};

void script_set_error(Script *script, const char *format, ...);

Script *script_load(Arena *arena, String path);
void script_destroy(Script *script);

b32 script_is_loaded(Script *script);
String script_error(Script *script);
String_Array script_functions(Script *script);
b32 script_has_function(Script *script, String name);
void script_set_invocation_options(Script *script, Script_Invocation_Options options);
b32 script_invoke(Script *script, String name);

#endif
