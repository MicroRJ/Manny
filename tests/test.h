#ifndef BOB_TEST_H
#define BOB_TEST_H

#include "bob_build.h"
#include "build_record_stream.h"
#include "script.h"
#include "compiler_command.h"
#include "make_depfile.h"
#include "logger.h"
#include "platform_adapter.h"
#include "platform.h"
#include "vcvars_cache.h"
#include "blake3.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                        \
	do {                                                                         \
		if (!(condition)) {                                                       \
			printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);       \
			return false;                                                          \
		}                                                                         \
	} while (0)

#define CHECK_OK(expression) CHECK((expression) == BOB_OK)
#define STRING_ARRAY_FROM(array) ((String_Array){ .items = (array), .count = ARRAY_COUNT(array) })

typedef struct Bob_Test
{
	const char *name;
	b32       (*function)(void);
}
Bob_Test;

#define BOB_TEST(function) { #function, function }

Bob_Node *test_add_node(Bob *graph, const char *name);
b32 test_run_tasks(Bob_Build *build, const Bob_Task_Desc *tasks, u32 task_count, u32 worker_count);
int test_run_suite(const char *name, const Bob_Test *tests, u32 count);

#endif
