#include "test.h"

static b32 get_test_executable(char *buffer, u32 buffer_size)
{
    DWORD length = GetModuleFileNameA(NULL, buffer, buffer_size);
    return length > 0 && length < buffer_size;
}

static b32 test_builder_runs_in_parallel(void)
{
    Bob_Build *build = bob_build_create();
    Bob *graph = bob_build_graph(build);
    Bob_Node *a = test_add_node(graph, "slow a");
    Bob_Node *b = test_add_node(graph, "slow b");
    Bob_Node *link = test_add_node(graph, "link");
    Bob_Task_Desc tasks[3] = {0};
    char executable[MAX_PATH];
    char command_a[2 * MAX_PATH];
    char command_b[2 * MAX_PATH];
    char command_link[2 * MAX_PATH];
    char event_a_name[128];
    char event_b_name[128];
    HANDLE event_a;
    HANDLE event_b;
    b32 executed;

    CHECK(get_test_executable(executable, sizeof(executable)));
    CHECK(snprintf(event_a_name, sizeof(event_a_name), "Local\\bob_graph_%lu_a",
                   GetCurrentProcessId()) > 0);
    CHECK(snprintf(event_b_name, sizeof(event_b_name), "Local\\bob_graph_%lu_b",
                   GetCurrentProcessId()) > 0);
    event_a = CreateEventA(NULL, TRUE, FALSE, event_a_name);
    event_b = CreateEventA(NULL, TRUE, FALSE, event_b_name);
    CHECK(event_a != NULL && event_b != NULL);

    CHECK(snprintf(command_a, sizeof(command_a), "\"%s\" --barrier %s %s a", executable, event_a_name, event_b_name) > 0);
    CHECK(snprintf(command_b, sizeof(command_b), "\"%s\" --barrier %s %s b", executable, event_b_name, event_a_name) > 0);
    CHECK(snprintf(command_link, sizeof(command_link), "\"%s\" --child 0 0 link", executable) > 0);

    tasks[0].command_line = string_from_cstring(command_a);
    tasks[1].command_line = string_from_cstring(command_b);
    tasks[2].command_line = string_from_cstring(command_link);

    CHECK_OK(bob_add_dependency(graph, link, a));
    CHECK_OK(bob_add_dependency(graph, link, b));

    executed = test_run_tasks(build, tasks, 3, 2);
    CloseHandle(event_a);
    CloseHandle(event_b);

    CHECK(executed);
    bob_build_destroy(build);
    return true;
}

typedef struct Build_Event_Test
{
	Bob_Node *node;
	u32       callback_thread;
	u32       started;
	u32       completed;
	b32       valid;
}
Build_Event_Test;

static void build_test_event(Bob_Event event, void *user_data)
{
	Build_Event_Test *test = user_data;
	if (platform_current_thread_id() != test->callback_thread || event.node != test->node) test->valid = false;
	if (event.type == BOB_EVENT_STARTED) ++test->started;
	else if (event.type == BOB_EVENT_COMPLETED) {
		if (!event.result.succeeded || !event.result.output) test->valid = false;
		++test->completed;
	}
	else test->valid = false;
}

static b32 test_builder_events(void)
{
	Bob_Build *build = bob_build_create();
	Bob *graph = bob_build_graph(build);
	Bob_Node *node = test_add_node(graph, "event task");
	Bob_Task_Desc task = {0};
	Build_Event_Test event = {
		.node = node,
		.callback_thread = platform_current_thread_id(),
		.valid = true,
	};
	char executable[MAX_PATH];
	char command[2 * MAX_PATH];

	CHECK(get_test_executable(executable, sizeof(executable)));
	CHECK(snprintf(command, sizeof(command), "\"%s\" --child 0 0 event", executable) > 0);
	task.command_line = string_from_cstring(command);
	CHECK_OK(bob_set_task(build, node, task));
	CHECK(bob_build(build, (Bob_Build_Params){
		.worker_count = 1,
		.user_data = &event,
		.event = build_test_event,
	}));
	CHECK(event.valid && event.started == 1 && event.completed == 1);
	bob_build_destroy(build);
	return true;
}

static b32 test_builder_propagates_failure(void)
{
    logger_set_muted(true);
    Bob_Build *build = bob_build_create();
    Bob *graph = bob_build_graph(build);
    Bob_Node *fail = test_add_node(graph, "fail");
    Bob_Node *blocked = test_add_node(graph, "blocked");
    Bob_Node *independent = test_add_node(graph, "independent");
    Bob_Task_Desc tasks[3] = {0};
    char executable[MAX_PATH];
    char fail_command[2 * MAX_PATH];
    char blocked_command[2 * MAX_PATH];
    char independent_command[2 * MAX_PATH];

    CHECK(get_test_executable(executable, sizeof(executable)));
    CHECK(snprintf(fail_command, sizeof(fail_command), "\"%s\" --child 0 1 fail", executable) > 0);
    CHECK(snprintf(blocked_command, sizeof(blocked_command), "\"%s\" --child 0 0 blocked", executable) > 0);
    CHECK(snprintf(independent_command, sizeof(independent_command), "\"%s\" --child 0 0 independent", executable) > 0);

    tasks[0].command_line = string_from_cstring(fail_command);
    tasks[1].command_line = string_from_cstring(blocked_command);
    tasks[2].command_line = string_from_cstring(independent_command);
    CHECK_OK(bob_add_dependency(graph, blocked, fail));

    CHECK(!test_run_tasks(build, tasks, 3, 2));
	CHECK(bob_task_state(build, fail) == BOB_NODE_FAILED);
	CHECK(bob_task_state(build, blocked) == BOB_NODE_BLOCKED);
	CHECK(bob_task_state(build, independent) == BOB_NODE_SUCCEEDED);
    bob_build_destroy(build);
    logger_set_muted(false);
    return true;
}

static b32 test_builder_reports_missing_executable(void)
{
    Bob_Build *build = bob_build_create();
    Bob *graph = bob_build_graph(build);
    Bob_Node *missing = test_add_node(graph, "missing executable");
    Bob_Task_Desc task = {
        .command_line = LIT("bob_executable_that_does_not_exist_7f31.exe --input x.c")
    };

    CHECK(!test_run_tasks(build, &task, 1, 1));
	CHECK(bob_task_state(build, missing) == BOB_NODE_FAILED);
    bob_build_destroy(build);
    return true;
}

static b32 test_builder_skips_existing_output(void)
{
    const char *output_path = "build\\incremental_test.out";
    String outputs[] = { string_from_cstring(output_path) };
    Bob_Build *first_build;
    Bob_Build *second_build;
    Bob *first_graph;
    Bob *second_graph;
    Bob_Task_Desc task = {0};
    Bob_Platform_File_Info info;

    DeleteFileA(output_path);
    task.command_line = LIT("cmd /c echo built>build\\incremental_test.out");
    task.outputs = STRING_ARRAY_FROM(outputs);

    first_build = bob_build_create();
    first_graph = bob_build_graph(first_build);
    test_add_node(first_graph, "create output");
    CHECK(test_run_tasks(first_build, &task, 1, 1));
	CHECK(bob_platform_file_info(string_from_cstring(output_path), &info));
    bob_build_destroy(first_build);

    second_build = bob_build_create();
    second_graph = bob_build_graph(second_build);
    test_add_node(second_graph, "skip existing output");
    CHECK(test_run_tasks(second_build, &task, 1, 1));
	CHECK(bob_task_state(second_build, bob_node_at(second_graph, 0)) == BOB_NODE_SUCCEEDED);
    bob_build_destroy(second_build);

    CHECK(DeleteFileA(output_path));
	return true;
}

static b32 test_directory_output_stays_clean(void)
{
	const char *directory = "build\\directory_output_test";
	const char *child = "build\\directory_output_test\\child.txt";
	String directory_outputs[] = { LIT("build/directory_output_test") };
	String child_outputs[] = { LIT("build/directory_output_test/child.txt") };
	Bob_Task_Desc tasks[2] = {
		{
			.command_line = LIT("cmd /c if not exist build\\directory_output_test mkdir build\\directory_output_test"),
			.outputs = STRING_ARRAY_FROM(directory_outputs),
		},
		{
			.command_line = LIT("cmd /c echo child>build\\directory_output_test\\child.txt"),
			.outputs = STRING_ARRAY_FROM(child_outputs),
		},
	};
	Bob_Platform_File_Info before;
	Bob_Platform_File_Info after;
	Bob_Build *build;
	Bob *graph;
	Bob_Node *prepare;
	Bob_Node *write_child;

	CHECK(platform_remove_tree(directory));
	build = bob_build_create();
	graph = bob_build_graph(build);
	CHECK(graph != NULL);
	prepare = test_add_node(graph, "prepare output directory");
	write_child = test_add_node(graph, "write child output");
	CHECK_OK(bob_add_dependency(graph, write_child, prepare));
	CHECK(test_run_tasks(build, tasks, 2, 1));
	bob_build_destroy(build);
	CHECK(bob_platform_file_info(string_from_cstring(child), &before));

	Sleep(20);
	build = bob_build_create();
	graph = bob_build_graph(build);
	CHECK(graph != NULL);
	prepare = test_add_node(graph, "prepare output directory");
	write_child = test_add_node(graph, "write child output");
	CHECK_OK(bob_add_dependency(graph, write_child, prepare));
	CHECK(test_run_tasks(build, tasks, 2, 1));
	bob_build_destroy(build);
	CHECK(bob_platform_file_info(string_from_cstring(child), &after));
	CHECK(after.modified_unix_ms == before.modified_unix_ms);
	CHECK(platform_remove_tree(directory));
	return true;
}

static b32 test_task_fingerprint_rebuilds(void)
{
	const char *output_path = "build\\fingerprint_test.out";
	String outputs[] = { LIT("build/fingerprint_test.out") };
	String first_includes[] = { LIT("include/first") };
	String second_includes[] = { LIT("include/second") };
	Bob_Task_Desc task = {
		.command_line = LIT("cmd /c echo first>build\\fingerprint_test.out"),
		.outputs = STRING_ARRAY_FROM(outputs),
		.include_directories = STRING_ARRAY_FROM(first_includes),
	};
	Bob_Platform_File_Info first;
	Bob_Platform_File_Info unchanged;
	Bob_Platform_File_Info command_changed;
	Bob_Platform_File_Info metadata_changed;
	Bob_Build *build;
	Bob *graph;

	CHECK(platform_remove_file(output_path));
	build = bob_build_create();
	graph = bob_build_graph(build);
	CHECK(graph != NULL);
	test_add_node(graph, "initial fingerprint");
	CHECK(test_run_tasks(build, &task, 1, 1));
	bob_build_destroy(build);
	CHECK(bob_platform_file_info(string_from_cstring(output_path), &first));

	Sleep(20);
	build = bob_build_create();
	graph = bob_build_graph(build);
	CHECK(graph != NULL);
	test_add_node(graph, "unchanged fingerprint");
	CHECK(test_run_tasks(build, &task, 1, 1));
	bob_build_destroy(build);
	CHECK(bob_platform_file_info(string_from_cstring(output_path), &unchanged));
	CHECK(unchanged.modified_unix_ms == first.modified_unix_ms);

	Sleep(20);
	task.command_line = LIT("cmd /c echo second>build\\fingerprint_test.out");
	build = bob_build_create();
	graph = bob_build_graph(build);
	CHECK(graph != NULL);
	test_add_node(graph, "changed command fingerprint");
	CHECK(test_run_tasks(build, &task, 1, 1));
	bob_build_destroy(build);
	CHECK(bob_platform_file_info(string_from_cstring(output_path), &command_changed));
	CHECK(command_changed.modified_unix_ms != unchanged.modified_unix_ms);

	Sleep(20);
	task.include_directories = STRING_ARRAY_FROM(second_includes);
	build = bob_build_create();
	graph = bob_build_graph(build);
	CHECK(graph != NULL);
	test_add_node(graph, "changed metadata fingerprint");
	CHECK(test_run_tasks(build, &task, 1, 1));
	bob_build_destroy(build);
	CHECK(bob_platform_file_info(string_from_cstring(output_path), &metadata_changed));
	CHECK(metadata_changed.modified_unix_ms != command_changed.modified_unix_ms);

	CHECK(platform_remove_file(output_path));
	return true;
}

static b32 write_test_file_at_time(const char *path, u64 time)
{
    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    ULARGE_INTEGER value;
    FILETIME file_time;
    b32 succeeded;

    if (file == INVALID_HANDLE_VALUE) return false;
    value.QuadPart = 116444736000000000ULL + time * 10000ULL;
    file_time.dwLowDateTime = value.LowPart;
    file_time.dwHighDateTime = value.HighPart;
    succeeded = SetFileTime(file, NULL, NULL, &file_time);
    CloseHandle(file);
    return succeeded;
}

static b32 test_newer_input_rebuilds(void)
{
	const char *input_path = "build\\newer_input_test.in";
	const char *output_path = "build\\newer_input_test.out";
    String inputs[] = { string_from_cstring(input_path) };
    String outputs[] = { string_from_cstring(output_path) };
	Bob_Task_Desc task = {0};
	Bob_Build *clean_build;
	Bob_Build *dirty_build;
	Bob *clean_graph;
	Bob *dirty_graph;
	Bob_Platform_File_Info output_info;

	CHECK(write_test_file_at_time(input_path, 0ULL));
	CHECK(platform_remove_file(output_path));
	task.command_line = LIT("cmd /c echo rebuilt>build\\newer_input_test.out");
    task.inputs = STRING_ARRAY_FROM(inputs);
    task.outputs = STRING_ARRAY_FROM(outputs);

	clean_build = bob_build_create();
    clean_graph = bob_build_graph(clean_build);
	test_add_node(clean_graph, "prime timestamp state");
	CHECK(test_run_tasks(clean_build, &task, 1, 1));
	bob_build_destroy(clean_build);

	CHECK(bob_platform_file_info(string_from_cstring(output_path), &output_info));
	CHECK(write_test_file_at_time(input_path, (u64)output_info.modified_unix_ms + 1000));
	dirty_build = bob_build_create();
	dirty_graph = bob_build_graph(dirty_build);
    test_add_node(dirty_graph, "dirty timestamps");
    CHECK(test_run_tasks(dirty_build, &task, 1, 1));
    bob_build_destroy(dirty_build);

    CHECK(DeleteFileA(input_path));
    CHECK(DeleteFileA(output_path));
    return true;
}

static b32 test_multiple_inputs_and_outputs(void)
{
    const char *input_a = "build\\multi_a.in";
    const char *input_b = "build\\multi_b.in";
    const char *output_a = "build\\multi_a.out";
    const char *output_b = "build\\multi_b.out";
    const char *marker = "build\\multi.marker";
    String inputs[] = { string_from_cstring(input_a), string_from_cstring(input_b) };
    String outputs[] = { string_from_cstring(output_a), string_from_cstring(output_b) };
    Bob_Task_Desc task = {0};
    Bob_Build *build;
    Bob *graph;
    Bob_Platform_File_Info info;

    DeleteFileA(marker);
    CHECK(write_test_file_at_time(input_a, 100ULL));
    CHECK(write_test_file_at_time(input_b, 150ULL));
	CHECK(platform_remove_file(output_a));
	CHECK(platform_remove_file(output_b));
	task.command_line = LIT("cmd /c echo a>build\\multi_a.out && echo b>build\\multi_b.out && echo rebuilt>build\\multi.marker");
    task.inputs = STRING_ARRAY_FROM(inputs);
    task.outputs = STRING_ARRAY_FROM(outputs);

	build = bob_build_create();
    graph = bob_build_graph(build);
	test_add_node(graph, "prime multiple files");
	CHECK(test_run_tasks(build, &task, 1, 1));
	bob_build_destroy(build);
	CHECK(DeleteFileA(marker));

	build = bob_build_create();
	graph = bob_build_graph(build);
	test_add_node(graph, "clean multiple files");
	CHECK(test_run_tasks(build, &task, 1, 1));
	CHECK(!bob_platform_file_info(string_from_cstring(marker), &info));
	bob_build_destroy(build);

	CHECK(bob_platform_file_info(string_from_cstring(output_a), &info));
	CHECK(write_test_file_at_time(input_b, (u64)info.modified_unix_ms + 1000));
	build = bob_build_create();
    graph = bob_build_graph(build);
    test_add_node(graph, "newest input wins");
	CHECK(test_run_tasks(build, &task, 1, 1));
	CHECK(bob_platform_file_info(string_from_cstring(marker), &info));
    bob_build_destroy(build);

    CHECK(DeleteFileA(output_b));
    CHECK(DeleteFileA(marker));
	build = bob_build_create();
    graph = bob_build_graph(build);
    test_add_node(graph, "one output missing");
	CHECK(test_run_tasks(build, &task, 1, 1));
	CHECK(bob_platform_file_info(string_from_cstring(output_b), &info));
	CHECK(bob_platform_file_info(string_from_cstring(marker), &info));
    bob_build_destroy(build);

    CHECK(DeleteFileA(input_a));
    CHECK(DeleteFileA(input_b));
    CHECK(DeleteFileA(output_a));
    CHECK(DeleteFileA(output_b));
    CHECK(DeleteFileA(marker));
    return true;
}

static b32 test_dependency_rebuild_propagates(void)
{
    const char *dependency_input = "build\\dependency.in";
    const char *dependency_output = "build\\dependency.out";
    const char *parent_output = "build\\parent.out";
    const char *marker = "build\\parent.marker";
    String dependency_inputs[] = { string_from_cstring(dependency_input) };
    String dependency_outputs[] = { string_from_cstring(dependency_output) };
    String parent_outputs[] = { string_from_cstring(parent_output) };
    Bob_Task_Desc tasks[2] = {0};
    Bob_Build *build;
    Bob *graph;
    Bob_Node *dependency;
    Bob_Node *parent;
    Bob_Platform_File_Info info;

    DeleteFileA(marker);
    CHECK(write_test_file_at_time(dependency_input, 100ULL));
	CHECK(platform_remove_file(dependency_output));
	CHECK(platform_remove_file(parent_output));

	tasks[0].command_line = LIT("cmd /c echo dependency>build\\dependency.out");
    tasks[0].inputs = STRING_ARRAY_FROM(dependency_inputs);
    tasks[0].outputs = STRING_ARRAY_FROM(dependency_outputs);
	tasks[1].command_line = LIT("cmd /c echo parent>build\\parent.out && echo rebuilt>build\\parent.marker");
    tasks[1].outputs = STRING_ARRAY_FROM(parent_outputs);

	build = bob_build_create();
    graph = bob_build_graph(build);
	dependency = test_add_node(graph, "prime dependency");
	parent = test_add_node(graph, "prime parent");
    CHECK_OK(bob_add_dependency(graph, parent, dependency));
	CHECK(test_run_tasks(build, tasks, 2, 1));
	bob_build_destroy(build);
	CHECK(DeleteFileA(marker));

	CHECK(bob_platform_file_info(string_from_cstring(dependency_output), &info));
	CHECK(write_test_file_at_time(dependency_input, (u64)info.modified_unix_ms + 1000));
	build = bob_build_create();
    graph = bob_build_graph(build);
    dependency = test_add_node(graph, "dirty dependency");
    parent = test_add_node(graph, "propagated parent");
    CHECK_OK(bob_add_dependency(graph, parent, dependency));
	CHECK(test_run_tasks(build, tasks, 2, 1));
	CHECK(bob_platform_file_info(string_from_cstring(marker), &info));
    bob_build_destroy(build);

    CHECK(DeleteFileA(dependency_input));
    CHECK(DeleteFileA(dependency_output));
    CHECK(DeleteFileA(parent_output));
    CHECK(DeleteFileA(marker));
    return true;
}

static b32 test_transparent_dependency(void)
{
	const char *parent_output = "build\\transparent_parent.out";
	String parent_outputs[] = { string_from_cstring(parent_output) };
	Bob_Task_Desc tasks[2] = {0};
	CHECK(platform_remove_file(parent_output));
	tasks[0].command_line = LIT("cmd /c exit /b 0");
	tasks[0].transparent = true;
	tasks[1].command_line = LIT("cmd /c echo parent>build\\transparent_parent.out");
	tasks[1].outputs = STRING_ARRAY_FROM(parent_outputs);
	Bob_Build *build = bob_build_create();
	Bob *graph = bob_build_graph(build);
	Bob_Node *dependency = test_add_node(graph, "prime transparent dependency");
	Bob_Node *parent = test_add_node(graph, "prime transparent parent");
	CHECK_OK(bob_add_dependency(graph, parent, dependency));
	CHECK(test_run_tasks(build, tasks, 2, 1));
	bob_build_destroy(build);
	Bob_Platform_File_Info before;
	Bob_Platform_File_Info after;
	CHECK(bob_platform_file_info(string_from_cstring(parent_output), &before));
	Sleep(20);
	build = bob_build_create();
	graph = bob_build_graph(build);
	dependency = test_add_node(graph, "transparent dependency");
	parent = test_add_node(graph, "clean transparent parent");
	CHECK_OK(bob_add_dependency(graph, parent, dependency));
	CHECK(test_run_tasks(build, tasks, 2, 1));
	bob_build_destroy(build);
	CHECK(bob_platform_file_info(string_from_cstring(parent_output), &after));
	CHECK(after.modified_unix_ms == before.modified_unix_ms);
	CHECK(DeleteFileA(parent_output));
	return true;
}

static b32 test_task_working_directory(void)
{
	const char *directory = "build\\task_working_directory";
	const char *resolved_output =
		"build\\task_working_directory\\result.out";
	const char *unresolved_output = "result.out";
	String outputs[] = { LIT("result.out") };
	Bob_Task_Desc task = {
		.command_line = LIT("cmd /c echo built>result.out"),
		.working_directory = LIT("build\\task_working_directory"),
		.outputs = STRING_ARRAY_FROM(outputs),
	};
	Bob_Build *build;
	Bob *graph;
	Bob_Platform_File_Info info;

	DeleteFileA(resolved_output);
	DeleteFileA(unresolved_output);
	if (!CreateDirectoryA(directory, NULL) &&
		GetLastError() != ERROR_ALREADY_EXISTS) return false;
	build = bob_build_create();
	graph = bob_build_graph(build);
	test_add_node(graph, "working directory output");
	CHECK(test_run_tasks(build, &task, 1, 1));
	bob_build_destroy(build);
	CHECK(bob_platform_file_info(string_from_cstring(resolved_output), &info));
	CHECK(!bob_platform_file_info(string_from_cstring(unresolved_output), &info));

	build = bob_build_create();
	graph = bob_build_graph(build);
	test_add_node(graph, "working directory incremental output");
	CHECK(test_run_tasks(build, &task, 1, 1));
	bob_build_destroy(build);

	CHECK(DeleteFileA(resolved_output));
	CHECK(RemoveDirectoryA(directory));
	return true;
}


int main(int argument_count, char **arguments)
{
	if (argument_count == 5 && strcmp(arguments[1], "--child") == 0) {
		DWORD delay = (DWORD)strtoul(arguments[2], NULL, 10);
		int exit_code = atoi(arguments[3]);
		Sleep(delay);
		printf("%s\n", arguments[4]);
		return exit_code;
	}
	if (argument_count == 5 && strcmp(arguments[1], "--barrier") == 0) {
		HANDLE own_event = OpenEventA(EVENT_MODIFY_STATE, FALSE, arguments[2]);
		HANDLE other_event = OpenEventA(SYNCHRONIZE, FALSE, arguments[3]);
		if (!own_event || !other_event) return 1;
		SetEvent(own_event);
		DWORD wait_result = WaitForSingleObject(other_event, 5000);
		CloseHandle(own_event);
		CloseHandle(other_event);
		if (wait_result != WAIT_OBJECT_0) return 1;
		printf("%s\n", arguments[4]);
		return 0;
	}
	static const Bob_Test tests[] = {
		BOB_TEST(test_builder_runs_in_parallel),
		BOB_TEST(test_builder_events),
		BOB_TEST(test_builder_propagates_failure),
		BOB_TEST(test_builder_reports_missing_executable),
		BOB_TEST(test_builder_skips_existing_output),
		BOB_TEST(test_directory_output_stays_clean),
		BOB_TEST(test_task_fingerprint_rebuilds),
		BOB_TEST(test_newer_input_rebuilds),
		BOB_TEST(test_multiple_inputs_and_outputs),
		BOB_TEST(test_dependency_rebuild_propagates),
		BOB_TEST(test_transparent_dependency),
		BOB_TEST(test_task_working_directory),
	};
	return test_run_suite("build", tests, ARRAY_COUNT(tests));
}
