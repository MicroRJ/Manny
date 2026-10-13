#include "compiler_command.h"

static String executable_name(String path)
{
	u64 start = 0;
	u64 i;
	for (i = 0; i < path.size; ++i) {
		if (path.data[i] == '/' || path.data[i] == '\\') start = i + 1;
	}
	path = string_slice(path, start, path.size - start);
	if (string_ends_with_insensitive(path, LIT(".exe"))) {
		path.size -= sizeof(".exe") - 1;
	}
	return path;
}

// Match every start against the compiler, the start is either 0 or the position
// right after a '-'.
static b32 compiler_name_matches(String name, String compiler)
{
	for (u64 start = 0; start + compiler.size <= name.size; ++start)
	{
		if (start > 0 && name.data[start - 1] != '-') {
			continue;
		}
		if (!string_equal_insensitive(string_slice(name, start, compiler.size), compiler)) {
			continue;
		}

		u64 end = start + compiler.size;
		if (end == name.size || name.data[end] == '-') {
			return true;
		}
	}
	return false;
}

static Compiler_Kind compiler_kind_from_executable(String executable)
{
	String name = executable_name(executable);
	if (compiler_name_matches(name, LIT("clang-cl"))) return COMPILER_KIND_CLANG_CL;
	if (compiler_name_matches(name, LIT("clang")) ||
		compiler_name_matches(name, LIT("clang++"))) return COMPILER_KIND_CLANG;
	if (compiler_name_matches(name, LIT("gcc")) ||
		compiler_name_matches(name, LIT("g++")) ||
		compiler_name_matches(name, LIT("cc")) ||
		compiler_name_matches(name, LIT("c++"))) return COMPILER_KIND_GCC;
	if (string_equal_insensitive(name, LIT("cl"))) return COMPILER_KIND_MSVC;
	return COMPILER_KIND_UNKNOWN;
}

b32 compiler_command_analyze(String executable, String_Array arguments, Compiler_Command *result)
{
	if (!result || !executable.data || (arguments.count && !arguments.items)) return false;
	*result = (Compiler_Command){0};
	result->executable = executable;
	result->kind = compiler_kind_from_executable(executable);
	for (u32 index = 0; index < arguments.count; ++index)
	{
		String argument = arguments.items[index];
		if (string_equal_insensitive(argument, LIT("/c")) ||
			string_equal(argument, LIT("-c"))) {
			result->compiles = true;
		}
		if (string_equal_insensitive(argument, LIT("/sourceDependencies")) ||
			string_equal(argument, LIT("-MMD")) ||
			string_equal(argument, LIT("-MD")) ||
			string_equal(argument, LIT("-MF")) ||
			string_starts_with(argument, LIT("-MF"))) {
			result->generates_dependencies = true;
		}
	}
	result->can_add_make_dependencies = result->compiles &&
		!result->generates_dependencies &&
		(result->kind == COMPILER_KIND_CLANG ||
		 result->kind == COMPILER_KIND_CLANG_CL ||
		 result->kind == COMPILER_KIND_GCC);
	return true;
}

b32 compiler_command_add_dependencies(Arena *arena, const Compiler_Command *command,
	String_Array arguments, String dependency_file, String_Array *result)
{
	u32 extra_count;
	if (!arena || !command || !result || (arguments.count && !arguments.items) || !dependency_file.data ||
		command->kind == COMPILER_KIND_UNKNOWN || !command->compiles ||
		command->generates_dependencies) {
		return false;
	}
	if (command->kind == COMPILER_KIND_CLANG_CL) extra_count = 2;
	else extra_count = 3;
	if (arguments.count > UINT32_MAX - extra_count) return false;
	String *items = arena_push_aligned(arena,
		(u64)(arguments.count + extra_count) * sizeof(*items), _Alignof(String));
	if (!items) return false;
	if (arguments.count) memcpy(items, arguments.items, (size_t)arguments.count * sizeof(*items));
	result->items = items;
	result->count = arguments.count;
	switch (command->kind) {
	case COMPILER_KIND_CLANG:
	case COMPILER_KIND_GCC:
		items[result->count++] = LIT("-MD");
		items[result->count++] = LIT("-MF");
		items[result->count++] = dependency_file;
		break;
	case COMPILER_KIND_CLANG_CL:
		items[result->count++] = LIT("/clang:-MD");
		{
			void *start = arena_top(arena);
			if (!arena_append_text(arena, "/clang:-MF") || !arena_append_str(arena, dependency_file)) return false;
			items[result->count] = arena_string_from(arena, start);
			arena_finalize_string(arena, items[result->count]);
			++result->count;
		}
		break;
	case COMPILER_KIND_MSVC:
		items[result->count++] = LIT("/sourceDependencies");
		items[result->count++] = dependency_file;
		break;
	default:
		return false;
	}
	return true;
}

