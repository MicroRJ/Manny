#include "test.h"

static b32 string_array_matches(String_Array actual, const char **expected, u32 count)
{
	if (actual.count != count) return false;
	for (u32 i = 0; i < count; ++i) {
		if (!string_equal(actual.items[i], string_from_cstring(expected[i]))) return false;
	}
	return true;
}

static b32 test_make_depfile(void)
{
	Arena arena = arena_create(KILOBYTES(64));
	String_Array dependencies;
	u64 mark;

	CHECK(arena.data != NULL);
	{
		const char *expected[] = { "src/main.c", "include/main.h" };
		CHECK(make_depfile_parse(&arena,
			LIT("build/main.o: src/main.c include/main.h\n"),
			&dependencies));
		CHECK(string_array_matches(dependencies, expected, ARRAY_COUNT(expected)));
	}

	arena_reset(&arena);
	{
		const char *expected[] = {
			"C:\\src\\main.c",
			"C:\\Program Files\\SDK\\header.h",
		};
		CHECK(make_depfile_parse(&arena,
			LIT(
				"C:\\build\\main.obj: C:\\src\\main.c \\\r\n"
				"  C:\\Program\\ Files\\SDK\\header.h\r\n"),
			&dependencies));
		CHECK(string_array_matches(dependencies, expected, ARRAY_COUNT(expected)));
	}

	arena_reset(&arena);
	{
		const char *expected[] = {
			"path with spaces/header.h",
			"hash#header.h",
			"cash$money.h",
			"colon:name.h",
			"slash\\name.h",
		};
		CHECK(make_depfile_parse(&arena,
			LIT(
				"out.o: path\\ with\\ spaces/header.h hash\\#header.h "
				"cash$$money.h colon\\:name.h slash\\\\name.h\n"),
			&dependencies));
		CHECK(string_array_matches(dependencies, expected, ARRAY_COUNT(expected)));
	}

	arena_reset(&arena);
	{
		const char *expected[] = { "one.c", "common.h" };
		CHECK(make_depfile_parse(&arena,
			LIT(
				"one.o one.d: one.c common.h common.h # ignored.h\n"
				"one.c:\n"
				"common.h:\n"),
			&dependencies));
		CHECK(string_array_matches(dependencies, expected, ARRAY_COUNT(expected)));
	}

	arena_reset(&arena);
	CHECK(make_depfile_parse(&arena, (String){0}, &dependencies));
	CHECK(dependencies.count == 0);
	mark = arena_mark(&arena);
	CHECK(!make_depfile_parse(&arena,
		LIT("build/main.o src/main.c\n"), &dependencies));
	CHECK(arena_mark(&arena) == mark);
	CHECK(dependencies.count == 0);

	arena_destroy(&arena);
	return true;
}

static b32 write_test_text_at_time(const char *path, const char *text, u64 time)
{
    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    size_t text_size = strlen(text);
    DWORD written = 0;
    ULARGE_INTEGER value;
    FILETIME file_time;
    b32 succeeded;

    if (file == INVALID_HANDLE_VALUE || text_size > UINT32_MAX) {
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        return false;
    }
    value.QuadPart = 116444736000000000ULL + time * 10000ULL;
    file_time.dwLowDateTime = value.LowPart;
    file_time.dwHighDateTime = value.HighPart;
    succeeded = WriteFile(file, text, (DWORD)text_size, &written, NULL) &&
                written == (DWORD)text_size &&
                SetFileTime(file, NULL, NULL, &file_time);
    CloseHandle(file);
    return succeeded;
}

static b32 run_single_task(const Manny_Task_Desc *task, const char *name)
{
	Manny_Build *build = manny_build_create();
	Manny *graph = manny_build_graph(build);
	b32 result;
	if (!graph) return false;
	test_add_node(graph, name);
	result = test_run_tasks(build, task, 1, 1);
	manny_build_destroy(build);
	return result;
}

static b32 test_compiler_dependency_state(void)
{
	static const char malformed[] = "{ version = 1, tasks = 7 }";
	Arena arena = arena_create(KILOBYTES(64));
	String original_directory = {0};
	String inputs[] = { LIT("source.c") };
	String outputs[] = { LIT("object.obj") };
	Manny_Task_Desc task = {
		.command_line = LIT("clang-cl /nologo /c source.c /Foobject.obj"),
		.working_directory = LIT("work"),
		.inputs = STRING_ARRAY_FROM(inputs),
		.outputs = STRING_ARRAY_FROM(outputs),
	};
	Manny_Platform_File_Info before;
	Manny_Platform_File_Info after;
	b32 changed_directory = false;
	b32 result = false;

#define CHECK_DEPENDENCY_STATE(condition)                                      \
	do {                                                                         \
		if (!(condition)) {                                                        \
			printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);          \
			goto cleanup;                                                           \
		}                                                                          \
	} while (0)

	CHECK_DEPENDENCY_STATE(arena.data);
	CHECK_DEPENDENCY_STATE(manny_platform_current_directory(&arena, &original_directory));
	CHECK_DEPENDENCY_STATE(platform_remove_tree("build\\compiler_dependency_state"));
	CHECK_DEPENDENCY_STATE(platform_create_directories("build\\compiler_dependency_state"));
	CHECK_DEPENDENCY_STATE(platform_set_current_directory("build\\compiler_dependency_state"));
	changed_directory = true;
	CHECK_DEPENDENCY_STATE(platform_create_directories("work"));
	CHECK_DEPENDENCY_STATE(write_test_text_at_time("work\\header.h", "#define VALUE 1\n", 100ULL));
	CHECK_DEPENDENCY_STATE(write_test_text_at_time("work\\source.c",
		"#include \"header.h\"\nint dependency_value = VALUE;\n", 100ULL));
	CHECK_DEPENDENCY_STATE(run_single_task(&task, "capture compiler dependencies"));
	CHECK_DEPENDENCY_STATE(manny_platform_file_info(LIT("work/object.obj"), &before));

	Sleep(20);
	CHECK_DEPENDENCY_STATE(run_single_task(&task, "reuse compiler dependencies"));
	CHECK_DEPENDENCY_STATE(manny_platform_file_info(LIT("work/object.obj"), &after));
	CHECK_DEPENDENCY_STATE(after.modified_unix_ms == before.modified_unix_ms);

	CHECK_DEPENDENCY_STATE(write_test_text_at_time("work\\header.h", "#define VALUE 2\n",
		(u64)after.modified_unix_ms + 1000));
	Sleep(20);
	CHECK_DEPENDENCY_STATE(run_single_task(&task, "rebuild changed compiler dependency"));
	CHECK_DEPENDENCY_STATE(manny_platform_file_info(LIT("work/object.obj"), &before));
	CHECK_DEPENDENCY_STATE(before.modified_unix_ms != after.modified_unix_ms);

	CHECK_DEPENDENCY_STATE(write_test_text_at_time("work\\header.h", "#define VALUE 3\n", 100ULL));
	CHECK_DEPENDENCY_STATE(platform_remove_file(".manny\\state"));
	Sleep(20);
	CHECK_DEPENDENCY_STATE(run_single_task(&task, "rebuild missing compiler state"));
	CHECK_DEPENDENCY_STATE(manny_platform_file_info(LIT("work/object.obj"), &after));
	CHECK_DEPENDENCY_STATE(after.modified_unix_ms != before.modified_unix_ms);

	CHECK_DEPENDENCY_STATE(manny_platform_write_entire_file(LIT(".manny/state"),
		malformed, sizeof(malformed) - 1));
	Sleep(20);
	CHECK_DEPENDENCY_STATE(run_single_task(&task, "rebuild malformed compiler state"));
	CHECK_DEPENDENCY_STATE(manny_platform_file_info(LIT("work/object.obj"), &before));
	CHECK_DEPENDENCY_STATE(before.modified_unix_ms != after.modified_unix_ms);

	CHECK_DEPENDENCY_STATE(platform_remove_file("work\\header.h"));
	CHECK_DEPENDENCY_STATE(!run_single_task(&task, "rebuild missing compiler dependency"));
	CHECK_DEPENDENCY_STATE(!manny_platform_file_info(
		LIT("work/object.obj.d.tmp"), &after));
	CHECK_DEPENDENCY_STATE(write_test_text_at_time("work\\header.h", "#define VALUE 4\n", 100ULL));
	Sleep(20);
	CHECK_DEPENDENCY_STATE(run_single_task(&task, "rebuild after failed compiler dependency"));
	CHECK_DEPENDENCY_STATE(manny_platform_file_info(LIT("work/object.obj"), &after));
	CHECK_DEPENDENCY_STATE(after.modified_unix_ms != before.modified_unix_ms);
	result = true;

cleanup:
	if (changed_directory) {
		platform_remove_tree(".manny");
		platform_remove_tree("work");
		if (!platform_set_current_directory(original_directory.data)) result = false;
		else platform_remove_tree("build\\compiler_dependency_state");
	}
	else platform_remove_tree("build\\compiler_dependency_state");
	arena_destroy(&arena);
#undef CHECK_DEPENDENCY_STATE
	return result;
}

static b32 test_option_resolution(void)
{
	Script_Options script = {
		.worker_count = 2,
		.verbosity = 0,
		.has_worker_count = true,
		.has_verbosity = true,
	};
	Cmd_Options command_line = {
		.verbosity = 3,
		.has_verbosity = true,
	};
	Script_Options merged = script_options_resolve(script, command_line);
	CHECK(merged.worker_count == 2);
	CHECK(merged.verbosity == 3);
	CHECK(merged.has_worker_count);
	CHECK(merged.has_verbosity);
	return true;
}

static b32 test_compiler_command(void)
{
	Arena arena = arena_create(KILOBYTES(64));
	Compiler_Command command;
	String augmented;
	CHECK(compiler_command_parse(&arena, LIT("   "), &command));
	CHECK(!command.executable.data && command.kind == COMPILER_KIND_UNKNOWN);
	CHECK(compiler_command_parse(&arena, LIT("/c source.c"), &command));
	CHECK(string_equal(command.executable, LIT("/c")) && !command.compiles);
	CHECK(compiler_command_parse(&arena, LIT("clang-cl /Ione /I \"two words\" -Ithree -I four -isystem system"), &command));
	CHECK(command.kind == COMPILER_KIND_CLANG_CL);
	CHECK(!command.compiles);
	CHECK(compiler_command_parse(&arena,
		LIT("\"C:\\Program Files\\LLVM\\bin\\clang-cl.exe\" /c source.c /Foobject.obj"),
		&command));
	CHECK(command.can_add_make_dependencies);
	CHECK(compiler_command_add_dependencies(&arena, &command,
		LIT("\"C:\\Program Files\\LLVM\\bin\\clang-cl.exe\" /c source.c /Foobject.obj"),
		LIT("object.obj.d"), &augmented));
	CHECK(string_ends_with(augmented, LIT(" /clang:-MD /clang:-MF\"object.obj.d\"")));
	CHECK(compiler_command_parse(&arena, LIT("cl.exe /nologo /c source.c"), &command));
	CHECK(command.kind == COMPILER_KIND_MSVC && command.compiles);
	CHECK(!command.can_add_make_dependencies);
	CHECK(compiler_command_add_dependencies(&arena, &command, LIT("cl /c source.c"),
		LIT("object.json"), &augmented));
	CHECK(string_ends_with(augmented, LIT(" /sourceDependencies \"object.json\"")));
	CHECK(compiler_command_parse(&arena, LIT("gcc -MMD -c source.c"), &command));
	CHECK(command.kind == COMPILER_KIND_GCC && command.compiles && command.generates_dependencies);
	CHECK(!command.can_add_make_dependencies);
	CHECK(!compiler_command_add_dependencies(&arena, &command,
		LIT("gcc -MMD -c source.c"),
		LIT("object.d"), &augmented));
	CHECK(compiler_command_parse(&arena,
		LIT("x86_64-w64-mingw32-gcc -c source.c -o object.o"), &command));
	CHECK(command.kind == COMPILER_KIND_GCC && command.compiles);
	CHECK(command.can_add_make_dependencies);
	CHECK(compiler_command_parse(&arena,
		LIT("C:\\toolchain\\aarch64-linux-gnu-g++.exe -c source.cpp -o object.o"), &command));
	CHECK(command.kind == COMPILER_KIND_GCC && command.compiles);
	CHECK(compiler_command_parse(&arena, LIT("notgcc -c source.c"), &command));
	CHECK(command.kind == COMPILER_KIND_UNKNOWN && command.compiles);
	arena_destroy(&arena);
	return true;
}


int main(void)
{
	static const Manny_Test tests[] = {
		MANNY_TEST(test_option_resolution),
		MANNY_TEST(test_compiler_command),
		MANNY_TEST(test_make_depfile),
		MANNY_TEST(test_compiler_dependency_state),
	};
	return test_run_suite("compiler", tests, ARRAY_COUNT(tests));
}
