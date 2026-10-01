#ifndef BUILD_RECORD_H
#define BUILD_RECORD_H

#include "manny_build_internal.h"

typedef struct Build_Record_Task
{
	Manny_Path        output;
	u64             output_stamp;
	Manny_Fingerprint fingerprint;
	/* Dependency storage is immutable and remains valid for the stream lifetime. */
	Manny_Path_Array  dependencies;
}
Build_Record_Task;

typedef struct Manny_Build_Snapshot
{
	const Build_Record_Task *tasks;
	u32                      task_count;
}
Manny_Build_Snapshot;

typedef enum Build_Record_Result
{
	BUILD_RECORD_OK,
	BUILD_RECORD_RECOVERED,
	BUILD_RECORD_MISSING,
	BUILD_RECORD_INVALID,
	BUILD_RECORD_ERROR,
}
Build_Record_Result;

typedef struct Manny_Build_Recorder
{
	Arena       *arena;
	Manny_Build *build;

	Build_Record_Task *tasks;
	u32                task_count;
	u32                task_capacity;

	Manny_Path *paths;
	u32         path_count;
	u32         path_capacity;

	u32        *ids_by_atom;
	u32         atom_capacity;
	b32         initialized;
}
Manny_Build_Recorder;

// TODO(RJ): we may want to start thinking about return a proper error code instead of booleans!

b32 manny_build_recorder_init(Manny_Build_Recorder *stream, Arena *arena, Manny_Build *build);
void manny_build_recorder_destroy(Manny_Build_Recorder *stream);
void manny_build_recorder_clear(Manny_Build_Recorder *stream);
b32 manny_build_recorder_append_set(Manny_Build_Recorder *stream, String path, Build_Record_Task task);
b32 manny_build_recorder_append_remove(Manny_Build_Recorder *stream, String path, Manny_Path output);
b32 manny_build_recorder_compact(Manny_Build_Recorder *stream, String path);
Build_Record_Result manny_build_recorder_load(Manny_Build_Recorder *stream, String path);


b32 manny_build_snapshot(const Manny_Build_Recorder *stream, Arena *arena, Manny_Build_Snapshot *snapshot);
b32 manny_build_snapshot_get(const Manny_Build_Snapshot *snapshot, Manny_Path output, Build_Record_Task *result);


#endif


