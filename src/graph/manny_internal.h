#ifndef MANNY_INTERNAL_H
#define MANNY_INTERNAL_H

#include "manny.h"

typedef struct Manny_Node_Array
{
	Manny_Node **items;
	u32          count;
	u32          capacity;
}
Manny_Node_Array;

struct Manny_Node
{
	// TODO(RJ): dynamic arrays are not needed here!
	Manny_Node_Array     dependencies;
	Manny_Node_Array     dependents;

	String               name;

	Manny_Node_Function *function;

	// TODO(RJ): remove this!
	u32                  index;
	// TODO(RJ): remove this!
	void                *user_data;
};

// NOTE(RJ): note that this is a graph builder at this point!
struct Manny
{
	Arena        arena;

	Manny_Node **nodes;
	u32          node_count;
	u32          node_capacity;

	// TODO(RJ): this can be removed entirely once every node is truly immutable, because
	// execution only cares about the active nodes at the moment of execution, if new nodes
	// are added it doesn't matter!
	b32        sealed;

	// TODO(RJ): this is literally just a reference counter so that we can't destroy the graph
	// while we're still executing, but execution could instead just create a copy of the graph
	// or something if nodes pointed by id or something and we actually produced a graph artifact.
	u32        execution_count;
};

b32 manny_valid_node(const Manny *manny, const Manny_Node *node);

#endif

