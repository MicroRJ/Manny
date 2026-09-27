#include "test.h"

static b32 test_elf_descriptor(void)
{
    static const char source[] =
        "compile_main := {\n"
        "    name = \"compile main\",\n"
        "    command_line = \"clang-cl /c main.c\",\n"
        "    working_directory = \".\",\n"
        "    inputs = {\"main.c\"},\n"
        "    outputs = {\"main.obj\"},\n"
        "    include_dirs = {\"include\"},\n"
        "    dependencies = {},\n"
        "}\n"
        "compile_message := {\n"
        "    name = \"compile message\",\n"
        "    command_line = \"clang-cl /c message.c\",\n"
        "    dependencies = {},\n"
        "}\n"
        "link := {\n"
        "    name = \"link hello.exe\",\n"
        "    command_line = \"clang-cl main.obj message.obj\",\n"
        "    dependencies = {compile_main, compile_message},\n"
        "}\n"
        "run := {\n"
        "    name = \"run hello.exe\",\n"
        "    command_line = \"hello.exe\",\n"
        "    dependencies = {link},\n"
        "}\n"
        "ret {\n"
		"    root = \"build\",\n"
        "    targets = {run},\n"
        "    options = {workers = 2, verbosity = 0},\n"
        "}\n";
    String path = LIT("build/test_elf_descriptor.elf");
    Script_Build build;
    Manny *graph;

    CHECK(platform_create_directories("build"));
    CHECK(manny_platform_write_entire_file(path, source, sizeof(source) - 1));
    if (!script_load_build(path, &build)) {
        platform_remove_file(path.data);
        printf("  elf error: %s\n", build.error);
        return false;
    }
    CHECK(platform_remove_file(path.data));
    graph = manny_build_graph(build.build);
	CHECK(string_ends_with(manny_path_string(build.build, manny_build_root(build.build)), LIT("/build")));
    CHECK(manny_task_count(build.build) == 4);
    CHECK(string_equal(string_from_cstring(manny_task_name(manny_node_at(graph, 0))), LIT("run hello.exe")));
    CHECK(manny_dependency_count(manny_node_at(graph, 0)) == 1);
    CHECK(manny_dependency(manny_node_at(graph, 0), 0) == manny_node_at(graph, 1));
    CHECK(string_equal(string_from_cstring(manny_task_name(manny_node_at(graph, 2))), LIT("compile main")));
    CHECK(build.options.has_worker_count);
    CHECK(build.options.worker_count == 2);
    CHECK(build.options.has_verbosity);
    CHECK(build.options.verbosity == 0);
    CHECK(manny_dependency_count(manny_node_at(graph, 1)) == 2);
    CHECK(manny_dependency(manny_node_at(graph, 1), 0) == manny_node_at(graph, 2));
    manny_build_destroy(build.build);
    return true;
}

static b32 test_elf_generated_descriptor(void)
{
    static const char source[] =
        "tasks := {}\n"
        "dependencies := {}\n"
        "for index := 0 ... 8 ? {\n"
        "    task := {\n"
        "        name = f\"generated ${index}\",\n"
        "        command_line = f\"generate ${index}\",\n"
        "        dependencies = dependencies,\n"
        "    }\n"
        "    tasks:add(task)\n"
        "    dependencies = {task}\n"
        "}\n"
        "ret {targets = {tasks[7]}}\n";
    String path = LIT("build/test_elf_generated_descriptor.elf");
    Script_Build build;
    Manny *graph;

    CHECK(platform_create_directories("build"));
    CHECK(manny_platform_write_entire_file(path, source, sizeof(source) - 1));
    if (!script_load_build(path, &build)) {
        platform_remove_file(path.data);
        printf("  elf error: %s\n", build.error);
        return false;
    }
    CHECK(platform_remove_file(path.data));
    graph = manny_build_graph(build.build);
    CHECK(manny_task_count(build.build) == 8);
    CHECK(string_equal(string_from_cstring(manny_task_name(manny_node_at(graph, 0))), LIT("generated 7")));
    CHECK(manny_dependency_count(manny_node_at(graph, 0)) == 1);
    CHECK(manny_dependency(manny_node_at(graph, 0), 0) == manny_node_at(graph, 1));
    CHECK(string_equal(string_from_cstring(manny_task_name(manny_node_at(graph, 7))), LIT("generated 0")));
    CHECK(manny_dependency_count(manny_node_at(graph, 7)) == 0);
    manny_build_destroy(build.build);
    return true;
}

static b32 test_manny_script(void)
{
    Arena arena = arena_create(MEGABYTES(16));
    Script *script = script_load(&arena, LIT("build.elf"));
    CHECK(script_is_loaded(script));
    CHECK(script_has_function(script, LIT("build")));
    CHECK(script_has_function(script, LIT("test")));
    CHECK(script_has_function(script, LIT("bless")));
    script_destroy(script);
    arena_destroy(&arena);
    return true;
}

// NOTE(RJ) we must be able to pet manny
static b32 test_pet_manny(void)
{
	Arena arena = arena_create(MEGABYTES(16));
	Script *script = script_load(&arena, LIT("build.elf"));
	CHECK(script_is_loaded(script));
	CHECK(script_has_function(script, LIT("pet")));
	CHECK(script_invoke(script, LIT("pet")));
	script_destroy(script);
	arena_destroy(&arena);
	return true;
}

static b32 test_script_functions(void)
{
    static const char source[] =
        "build := fun() {\n"
        "    ret manny.build({\n"
        "        targets = {},\n"
        "        options = {workers = 1, verbosity = 0},\n"
        "    })\n"
        "}\n"
        "clean := fun() { ret 1 }\n"
        "ret {build = build, clean = clean}\n";
    String path = LIT("build/test_script_functions.elf");
    Arena arena = arena_create(MEGABYTES(16));
    CHECK(platform_create_directories("build"));
    CHECK(manny_platform_write_entire_file(path, source, sizeof(source) - 1));
    Script *script = script_load(&arena, path);
    CHECK(platform_remove_file(path.data));
    CHECK(script_is_loaded(script));
    String_Array functions = script_functions(script);
    CHECK(functions.count == 2);
    CHECK(script_has_function(script, LIT("build")));
    CHECK(script_has_function(script, LIT("clean")));
    CHECK(!script_has_function(script, LIT("missing")));
    CHECK(script_invoke(script, LIT("build")));
    CHECK(script_invoke(script, LIT("clean")));
    CHECK(!script_invoke(script, LIT("missing")));
    script_destroy(script);
    arena_destroy(&arena);
    return true;
}


int main(void)
{
	static const Manny_Test tests[] = {
		MANNY_TEST(test_elf_descriptor),
		MANNY_TEST(test_elf_generated_descriptor),
		MANNY_TEST(test_manny_script),
		MANNY_TEST(test_pet_manny),
		MANNY_TEST(test_script_functions),
	};
	return test_run_suite("script", tests, ARRAY_COUNT(tests));
}
