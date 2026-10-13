#ifndef COMPILER_COMMAND_H
#define COMPILER_COMMAND_H

#include "base.h"

typedef enum Compiler_Kind
{
	COMPILER_KIND_UNKNOWN,
	COMPILER_KIND_CLANG,
	COMPILER_KIND_CLANG_CL,
	COMPILER_KIND_GCC,
	COMPILER_KIND_MSVC,
}
Compiler_Kind;

typedef struct Compiler_Command
{
	Compiler_Kind kind;
	String        executable;
	b32           compiles;
	b32           generates_dependencies;
	b32           can_add_make_dependencies;
}
Compiler_Command;

b32 compiler_command_analyze(String executable, String_Array arguments, Compiler_Command *result);
b32 compiler_command_add_dependencies(Arena *arena, const Compiler_Command *command,
	String_Array arguments, String dependency_file, String_Array *result);

#endif

