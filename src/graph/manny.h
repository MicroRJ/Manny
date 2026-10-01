#ifndef MANNY_H
#define MANNY_H

#include "base.h"

// TODO(RJ): remove this from here!
#define MANNY_VERSION "0.3.0-dev"

typedef struct Manny Manny;
typedef struct Manny_Node Manny_Node;
typedef struct Manny_Execution Manny_Execution;
typedef struct Manny_Node_Context Manny_Node_Context;

typedef enum Manny_Error
{
	MANNY_OK,
	MANNY_ERROR_OUT_OF_MEMORY,
	MANNY_ERROR_INVALID_TASK,
	MANNY_ERROR_DUPLICATE_DEPENDENCY,
	MANNY_ERROR_SELF_DEPENDENCY,
	MANNY_ERROR_GRAPH_SEALED,
	MANNY_ERROR_INVALID_STATE,
	MANNY_ERROR_CYCLE,
}
Manny_Error;

#define MANNY_ERROR_INVALID_NODE MANNY_ERROR_INVALID_TASK

typedef enum Manny_Node_Status
{
	MANNY_NODE_PENDING,
	MANNY_NODE_READY,
	MANNY_NODE_RUNNING,
	MANNY_NODE_SUCCEEDED,
	MANNY_NODE_FAILED,
	MANNY_NODE_BLOCKED,
}
Manny_Node_Status;

/* Output allocated from the node context arena remains valid until manny_execution_destroy. */
typedef struct Manny_Node_Result
{
	void *output;
	b32   succeeded;
	b32   changed;
}
Manny_Node_Result;

typedef Manny_Node_Result Manny_Node_Function(Manny_Node_Context *context, void *user_data);

struct Manny_Node_Context
{
	Manny_Execution *execution;
	Manny           *manny;
	Manny_Node      *node;
	Arena         *arena;
	void          *execution_data;
};

typedef struct Manny_Node_Desc
{
	String               name;
	Manny_Node_Function *function;
	void                *user_data;
}
Manny_Node_Desc;

typedef enum Manny_Event_Type
{
	MANNY_EVENT_STARTED,
	MANNY_EVENT_COMPLETED,
}
Manny_Event_Type;

typedef struct Manny_Event
{
	Manny_Event_Type  type;
	Manny_Node       *node;
	/* Meaningful only for MANNY_EVENT_COMPLETED. */
	Manny_Node_Result result;
}
Manny_Event;

typedef void Manny_Event_Function(Manny_Event event, void *user_data);

typedef struct Manny_Exec_Params
{
	u32                   worker_count;
	void                 *user_data;
	Manny_Event_Function *event;
}
Manny_Exec_Params;

Manny *manny_create(void);
void manny_destroy(Manny *manny);

// TODO(RJ): remove this entirely!
void *manny_allocate(Manny *manny, u64 size, u64 alignment);
// TODO(RJ): remove this entirely!
String manny_copy_string(Manny *manny, String string);

Manny_Error manny_add_node(Manny *manny, Manny_Node_Desc description, Manny_Node **node_out);

// TODO(RJ): these are to be removed entirely, nodes will have the deps capacity
// fixed at creation time, there's no need for node's to remain dynamic.
// Eventually, nodes will become entirely readonly.
Manny_Error manny_set_node(Manny *manny, Manny_Node *node, Manny_Node_Desc description);
Manny_Error manny_set_node_action(Manny *manny, Manny_Node *node, Manny_Node_Function *function, void *user_data);
Manny_Error manny_add_dependency(Manny *manny, Manny_Node *node, Manny_Node *dependency);

b32 manny_is_sealed(const Manny *manny);
u32 manny_node_count(const Manny *manny);
Manny_Node *manny_node_at(const Manny *manny, u32 index);
const char *manny_node_name(const Manny_Node *node);
Manny_Node_Function *manny_node_function(const Manny_Node *node);
u32 manny_dependency_count(const Manny_Node *node);
Manny_Node *manny_dependency(const Manny_Node *node, u32 index);

// TODO(RJ): remove this too, build tasks use user_data why couldn't we allocate parallel arrays?
void *manny_node_user_data(const Manny_Node *node);

Manny_Error manny_execution_create(Manny *manny, Manny_Execution **execution_out);
void manny_execution_destroy(Manny_Execution *execution);
b32 manny_execution_take_ready(Manny_Execution *execution, Manny_Node **node_out);
Manny_Error manny_execution_complete_result(Manny_Execution *execution, Manny_Node *node, Manny_Node_Result result);
Manny_Error manny_execution_complete(Manny_Execution *execution, Manny_Node *node, b32 succeeded);
b32 manny_execution_is_finished(const Manny_Execution *execution);
b32 manny_execution_has_failed(const Manny_Execution *execution);
Manny_Node_Status manny_execution_node_state(const Manny_Execution *execution, const Manny_Node *node);
Manny_Node_Result manny_execution_node_result(const Manny_Execution *execution, const Manny_Node *node);
b32 manny_execute(Manny_Execution *execution, Manny_Exec_Params options);

const char *manny_error_string(Manny_Error result);

#endif

