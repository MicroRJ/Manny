#include "test.h"

Bob_Node *test_add_node(Bob *graph, const char *name)
{
	Bob_Node *node = NULL;
	Bob_Error result = bob_add_node(graph, (Bob_Node_Desc){ .name = string_from_cstring(name) }, &node);
	if (result != BOB_OK) {
		printf("  unable to add node %s: %s\n", name, bob_error_string(result));
		exit(2);
	}
	return node;
}

b32 test_run_tasks(Bob_Build *build, const Bob_Task_Desc *tasks, u32 task_count, u32 worker_count)
{
	Bob *graph = bob_build_graph(build);
	if (bob_task_count(build) != task_count) return false;
	for (u32 i = 0; i < task_count; ++i) {
		if (bob_set_task(build, bob_node_at(graph, i), tasks[i]) != BOB_OK) return false;
	}
	return bob_build(build, (Bob_Build_Params){ .worker_count = worker_count });
}

int test_run_suite(const char *name, const Bob_Test *tests, u32 count)
{
	u32 failed = 0;
	logger_init();
	printf("%s\n", name);
	for (u32 i = 0; i < count; ++i) {
		printf("  %-44s", tests[i].name);
		fflush(stdout);
		if (tests[i].function()) printf("PASS\n");
		else ++failed;
	}
	printf("\n%u/%u tests passed\n", count - failed, count);
	return failed ? 1 : 0;
}
