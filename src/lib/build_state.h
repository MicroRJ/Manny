#ifndef BUILD_STATE_H
#define BUILD_STATE_H

// NOTE(RJ):
//
// The build state is meant to be the runtime database of all the tasks
// we currently have.
//
// The build state is meant to be durable, the problem is that program can
// quit unexpectedly.
//
// For this we use the build_state_stream, it's a layer on top of the build
// state that journals each operation done on the build state.
//

#include "bob_build_internal.h"
#include "platform.h"

typedef struct Build_State_Task
{
	// TODO(RJ): we use a linear search, which should be fine for now!
	Bob_Path        output;

	u64             output_stamp;
	Bob_Fingerprint fingerprint;

	// Dependency storage is immutable and remains valid until the state arena is destroyed.
	Bob_Path_Array  dependencies;
}
Build_State_Task;

// --NOTE(RJ) Aug 24, 2026: mutex removed from state.
//
// We were doing something something quite silly!
// When bob starts we load the build state; then as each thread completes and pushes
// an event, the main thread reads it, and issues the appropriate stream commands.
//
// But we also update the build state we started with, the same one the worker threads
// are reading.
//
// We don't have to do that, we can create a copy of the build
// state and instead update that one!
//
typedef struct Build_State
{

	// Backing storage must outlive the state.
	Arena               *arena;

	Build_State_Task    *tasks;
	u32                  task_count;
	u32                  task_capacity;
	b32                  initialized;
}
Build_State;

b32 build_state_init(Build_State *state, Arena *arena);
void build_state_destroy(Build_State *state);
void build_state_clear(Build_State *state);

// NOTE(RJ): the result is a copy!
b32 build_state_get(Build_State *state, Bob_Path output, Build_State_Task *result);

// NOTE(RJ): these are only used for tests!
b32 build_state_set(Build_State *state, Bob_Path output, Bob_Path_Array dependencies, Bob_Fingerprint fingerprint);
b32 build_state_remove(Build_State *state, Bob_Path output);

#endif
