#ifndef MANNY_TEST_H
#define MANNY_TEST_H

#include "manny_build.h"
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

#define CHECK_OK(expression) CHECK((expression) == MANNY_OK)
#define STRING_ARRAY_FROM(array) ((String_Array){ .items = (array), .count = ARRAY_COUNT(array) })

typedef struct Manny_Test
{
	const char *name;
	b32       (*function)(void);
}
Manny_Test;

#define MANNY_TEST(function) { #function, function }

Manny_Node *test_add_node(Manny *graph, const char *name);
b32 test_run_tasks(Manny_Build *build, const Manny_Task_Desc *tasks, u32 task_count, u32 worker_count);
int test_run_suite(const char *name, const Manny_Test *tests, u32 count);

#endif
