#include "manny_internal.h"

#include <string.h>

static void *manny_push(Manny *manny, u64 size, u64 alignment)
{
	return arena_push_zero_aligned(&manny->arena, size, alignment);
}

static b32 manny_reserve(Manny *manny, void **memory, u32 element_size, u32 count, u32 *capacity, u32 needed, u64 alignment)
{
	size_t new_capacity;
	void *new_memory;
	if (*capacity >= needed) return true;
	new_capacity = *capacity ? *capacity : 8;
	while (new_capacity < needed) {
		if (new_capacity > SIZE_MAX / 2) return false;
		new_capacity *= 2;
	}
	if (new_capacity > SIZE_MAX / element_size) return false;
	new_memory = manny_push(manny, new_capacity * element_size, alignment);
	if (!new_memory) return false;
	if (*memory && count) memcpy(new_memory, *memory, count * element_size);
	*memory = new_memory;
	*capacity = (u32)new_capacity;
	return true;
}

void *manny_allocate(Manny *manny, u64 size, u64 alignment)
{
	if (!manny || manny->sealed || alignment == 0) return NULL;
	return manny_push(manny, size, alignment);
}

String manny_copy_string(Manny *manny, String string)
{
	if (!manny || manny->sealed || (!string.data && string.size)) return (String){0};
	return str_push_copy(&manny->arena, string);
}

static b32 node_array_push(Manny *manny, Manny_Node_Array *array, Manny_Node *node)
{
	if (!manny_reserve(manny, (void **)&array->items, sizeof(*array->items), array->count, &array->capacity, array->count + 1, _Alignof(Manny_Node *))) return false;
	array->items[array->count++] = node;
	return true;
}

b32 manny_valid_node(const Manny *manny, const Manny_Node *node)
{
	return manny && node && node->index < manny->node_count && manny->nodes[node->index] == node;
}

Manny *manny_create(void)
{
	Arena arena = arena_create(0);
	if (!arena.data) return NULL;
	arena_set_name(&arena, "Manny graph");
	Manny *manny = arena_push_zero_aligned(&arena, sizeof(*manny), _Alignof(Manny));
	if (!manny) {
		arena_destroy(&arena);
		return NULL;
	}
	manny->arena = arena;
	return manny;
}

void manny_destroy(Manny *manny)
{
	ASSERT(manny);
	ASSERT(manny->execution_count == 0);
	Arena arena = manny->arena;
	arena_destroy(&arena);
}

Manny_Error manny_add_node(Manny *manny, Manny_Node_Desc description, Manny_Node **node_out)
{
	ASSERT(manny);

	if (!description.name.data || !node_out) return MANNY_ERROR_INVALID_NODE;
	// TODO(RJ): we can remove this check once we make nodes truly immutable!
	if (manny->sealed) return MANNY_ERROR_GRAPH_SEALED;

	if (!manny_reserve(manny, (void **)&manny->nodes, sizeof(*manny->nodes), manny->node_count, &manny->node_capacity, manny->node_count + 1, _Alignof(Manny_Node *)))
	{
		return MANNY_ERROR_OUT_OF_MEMORY;
	}

	Manny_Node *node = manny_push(manny, sizeof(*node), _Alignof(Manny_Node));
	if (!node) return MANNY_ERROR_OUT_OF_MEMORY;

	node->name = str_push_copy(&manny->arena, description.name);
	if (!node->name.data) return MANNY_ERROR_OUT_OF_MEMORY;

	node->index = manny->node_count;
	node->function = description.function;
	node->user_data = description.user_data;

	manny->nodes[manny->node_count ++] = node;

	*node_out = node;
	return MANNY_OK;
}

// TODO(RJ): remove this entirely, only tests use this thing for whatever reason!
Manny_Error manny_set_node(Manny *manny, Manny_Node *node, Manny_Node_Desc description)
{
	String name;
	if (!manny_valid_node(manny, node)) return MANNY_ERROR_INVALID_NODE;
	if (manny->sealed) return MANNY_ERROR_GRAPH_SEALED;
	name = node->name;
	if (description.name.data) {
		name = str_push_copy(&manny->arena, description.name);
		if (!name.data) return MANNY_ERROR_OUT_OF_MEMORY;
	}
	node->name = name;
	node->function = description.function;
	node->user_data = description.user_data;
	return MANNY_OK;
}

Manny_Error manny_set_node_action(Manny *manny, Manny_Node *node, Manny_Node_Function *function, void *user_data)
{
	if (!manny_valid_node(manny, node)) return MANNY_ERROR_INVALID_NODE;
	if (manny->sealed) return MANNY_ERROR_GRAPH_SEALED;
	node->function = function;
	node->user_data = user_data;
	return MANNY_OK;
}

Manny_Error manny_add_dependency(Manny *manny, Manny_Node *node, Manny_Node *dependency)
{
	if (!manny_valid_node(manny, node) || !manny_valid_node(manny, dependency)) return MANNY_ERROR_INVALID_NODE;
	if (manny->sealed) return MANNY_ERROR_GRAPH_SEALED;
	if (node == dependency) return MANNY_ERROR_SELF_DEPENDENCY;
	for (u32 i = 0; i < node->dependencies.count; ++i) {
		if (node->dependencies.items[i] == dependency) return MANNY_ERROR_DUPLICATE_DEPENDENCY;
	}
	if (!node_array_push(manny, &node->dependencies, dependency)) return MANNY_ERROR_OUT_OF_MEMORY;
	if (!node_array_push(manny, &dependency->dependents, node)) {
		--node->dependencies.count;
		return MANNY_ERROR_OUT_OF_MEMORY;
	}
	return MANNY_OK;
}

b32 manny_is_sealed(const Manny *manny)
{
	ASSERT(manny);
	return manny->sealed;
}

u32 manny_node_count(const Manny *manny)
{
	ASSERT(manny);
	return manny->node_count;
}

Manny_Node *manny_node_at(const Manny *manny, u32 index)
{
	ASSERT(manny);
	ASSERT(index < manny->node_count);
	return manny->nodes[index];
}

// TODO(RJ) why is this returning a raw c string
const char *manny_node_name(const Manny_Node *node)
{
	ASSERT(node);
	return node->name.data;
}

Manny_Node_Function *manny_node_function(const Manny_Node *node)
{
	ASSERT(node);
	return node->function;
}

void *manny_node_user_data(const Manny_Node *node)
{
	ASSERT(node);
	return node->user_data;
}

u32 manny_dependency_count(const Manny_Node *node)
{
	ASSERT(node);
	return node->dependencies.count;
}

Manny_Node *manny_dependency(const Manny_Node *node, u32 index)
{
	ASSERT(node);
	ASSERT(index < node->dependencies.count);
	return node->dependencies.items[index];
}

const char *manny_error_string(Manny_Error result)
{
	switch (result) {
		case MANNY_OK:                         return "ok";
		case MANNY_ERROR_OUT_OF_MEMORY:        return "out of memory";
		case MANNY_ERROR_INVALID_TASK:         return "invalid node";
		case MANNY_ERROR_DUPLICATE_DEPENDENCY: return "duplicate dependency";
		case MANNY_ERROR_SELF_DEPENDENCY:      return "self dependency";
		case MANNY_ERROR_GRAPH_SEALED:         return "Manny graph is sealed";
		case MANNY_ERROR_INVALID_STATE:        return "invalid node state";
		case MANNY_ERROR_CYCLE:                return "dependency cycle";
	}
	return "unknown Manny result";
}

