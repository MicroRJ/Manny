#include "test.h"

Manny_Node *test_add_node(Manny *graph, const char *name)
{
	Manny_Node *node = NULL;
	Manny_Error result = manny_add_node(graph, (Manny_Node_Desc){ .name = string_from_cstring(name) }, &node);
	if (result != MANNY_OK) {
		printf("  unable to add node %s: %s\n", name, manny_error_string(result));
		exit(2);
	}
	return node;
}

b32 test_run_tasks(Manny_Build *build, const Manny_Task_Desc *tasks, u32 task_count, u32 worker_count)
{
	Manny *graph = manny_build_graph(build);
	if (manny_task_count(build) != task_count) return false;
	for (u32 i = 0; i < task_count; ++i) {
		if (manny_set_task(build, manny_node_at(graph, i), tasks[i]) != MANNY_OK) return false;
	}
	return manny_build(build, (Manny_Build_Params){ .worker_count = worker_count });
}

int test_run_suite(const char *name, const Manny_Test *tests, u32 count)
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
