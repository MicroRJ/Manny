#ifndef BUILD_RECORD_STREAM_H
#define BUILD_RECORD_STREAM_H

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

typedef struct Build_Record_Snapshot
{
	const Build_Record_Task *tasks;
	u32                      task_count;
}
Build_Record_Snapshot;

typedef enum Build_Record_Result
{
	BUILD_RECORD_OK,
	BUILD_RECORD_RECOVERED,
	BUILD_RECORD_MISSING,
	BUILD_RECORD_INVALID,
	BUILD_RECORD_ERROR,
}
Build_Record_Result;

typedef struct Build_Record_Stream
{
	Arena     *arena;
	Manny_Build *build;

	Build_Record_Task *tasks;
	u32                task_count;
	u32                task_capacity;

	Manny_Path *paths;
	u32       path_count;
	u32       path_capacity;
	u32      *ids_by_atom;
	u32       atom_capacity;
	b32       initialized;
}
Build_Record_Stream;

b32 build_record_stream_init(Build_Record_Stream *stream, Arena *arena, Manny_Build *build);
void build_record_stream_destroy(Build_Record_Stream *stream);
void build_record_stream_clear(Build_Record_Stream *stream);

b32 build_record_stream_snapshot(const Build_Record_Stream *stream, Arena *arena, Build_Record_Snapshot *snapshot);
b32 build_record_snapshot_get(const Build_Record_Snapshot *snapshot, Manny_Path output, Build_Record_Task *result);

b32 build_record_stream_append_set(Build_Record_Stream *stream, String path, Build_Record_Task task);
b32 build_record_stream_append_remove(Build_Record_Stream *stream, String path, Manny_Path output);
b32 build_record_stream_compact(Build_Record_Stream *stream, String path);
Build_Record_Result build_record_stream_load(Build_Record_Stream *stream, String path);

#endif
