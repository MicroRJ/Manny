#ifndef BOB_INTERNAL_H
#define BOB_INTERNAL_H

#include "bob.h"

// TODO(RJ): dynamic arrays aren't needed here because at node construction time, we know how many inputs & outputs.
// The problem is that we don't know the pointers yet at creation time, so we'd still have to patch the pointers,
// but, if we used an id system, the user could preserve the node ids, then initially an immutable node.
// For instance:
//
//	node_a := bob_gen_id(bob)
//	node_b := bob_gen_id(bob)
//	node_c := bob_gen_id(bob)
// bob_create_node(bob, node_a, [node_b, node_c])
//
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
