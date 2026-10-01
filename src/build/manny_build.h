#ifndef MANNY_BUILD_H
#define MANNY_BUILD_H

#include "manny.h"

#define MANNY_FINGERPRINT_SIZE 32

typedef struct Manny_Build Manny_Build;

typedef struct Manny_Fingerprint
{
	u8 bytes[MANNY_FINGERPRINT_SIZE];
}
Manny_Fingerprint;

// User fed task descriptor; the internal runtime representation is normalized.
typedef struct Manny_Task_Desc
{
	String       name;
	// TODO(RJ): replace this with the executable name, and arguments instead!
	String       command_line;
	String       working_directory;
	String_Array inputs;
	String_Array outputs;
	String_Array include_directories;
	b32          transparent;
}
Manny_Task_Desc;

typedef struct Manny_Build_Params
{
	u32                   worker_count;
	b32                   explain;
	/* Events are delivered on the thread calling manny_build. */
	void                 *user_data;
	Manny_Event_Function *event;
}
Manny_Build_Params;

Manny_Build *manny_build_create(void);
Manny_Build *manny_build_create_at(String root);
void manny_build_destroy(Manny_Build *build);
Manny *manny_build_graph(Manny_Build *build);
const Manny *manny_build_graph_const(const Manny_Build *build);

b32 manny_build(Manny_Build *build, Manny_Build_Params options);

Manny_Error manny_add_task(Manny_Build *build, Manny_Task_Desc task, Manny_Node **node_out);
Manny_Error manny_set_task(Manny_Build *build, Manny_Node *node, Manny_Task_Desc task);

u32 manny_task_count(const Manny_Build *build);
const char *manny_task_name(const Manny_Node *node);
Manny_Node_Status manny_task_state(const Manny_Build *build, const Manny_Node *node);

#endif

