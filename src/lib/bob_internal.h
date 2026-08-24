#ifndef BOB_INTERNAL_H
#define BOB_INTERNAL_H

#include "bob.h"

typedef struct Bob_Node_Array
{
	Bob_Node **items;
	u32        count;
	u32        capacity;
}
Bob_Node_Array;

struct Bob_Node
{
	// TODO(RJ): dynamic names are not needed here!
	Bob_Node_Array     dependencies;
	Bob_Node_Array     dependents;

	String             name;

	Bob_Node_Function *function;

	// TODO(RJ): remove this!
	u32                index;
	// TODO(RJ): this can be removed entirely!
	void              *user_data;
};

// NOTE(RJ): Note that this is just the graph builder!
struct Bob
{
	Arena      arena;
	Bob_Node **nodes;
	u32        node_count;
	u32        node_capacity;
	// TODO(RJ): this can be removed entirely once every node is truly immutable, because
	// execution only cares about the active nodes at the moment of execution, if new nodes
	// are added it doesn't matter!
	b32        sealed;
	// TODO(RJ): this is literally just a reference counter so that we can't destroy the graph
	// while we're still executing, but execution could instead just create a copy of the graph
	// or something if nodes pointed by id or something and we actually produced a graph artifact.
	u32        execution_count;
};

b32 bob_valid_node(const Bob *bob, const Bob_Node *node);

#endif
