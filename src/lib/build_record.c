#include "build_record_internal.h"

#include <string.h>

b32 build_record_reserve_tasks(Build_Record_Stream *stream, u32 needed)
{
	if (stream->task_capacity >= needed) return true;
	u32 capacity = stream->task_capacity ? stream->task_capacity : 16;
	while (capacity < needed) {
		if (capacity > UINT32_MAX / 2) return false;
		capacity *= 2;
	}
	Build_Record_Task *tasks = arena_push_zero_aligned(stream->arena, (u64)capacity * sizeof(*tasks), _Alignof(Build_Record_Task));
	if (!tasks) return false;
	if (stream->task_count) memcpy(tasks, stream->tasks, (u64)stream->task_count * sizeof(*stream->tasks));
	stream->tasks = tasks;
	stream->task_capacity = capacity;
	return true;
}

u32 build_record_task_index(const Build_Record_Stream *stream, Bob_Path output)
{
	if (!stream || !bob_path_is_valid(output)) return UINT32_MAX;
	for (u32 i = 0; i < stream->task_count; ++i) {
		if (stream->tasks[i].output.atom.id == output.atom.id) return i;
	}
	return UINT32_MAX;
}

b32 build_record_set(Build_Record_Stream *stream, Build_Record_Task task)
{
	if (!stream || !stream->arena || !bob_path_is_valid(task.output)) return false;
	if (task.dependencies.count && !task.dependencies.items) return false;
	for (u32 i = 0; i < task.dependencies.count; ++i) {
		if (!bob_path_is_valid(task.dependencies.items[i])) return false;
	}
	u32 existing = build_record_task_index(stream, task.output);
	if (existing == UINT32_MAX && stream->task_count == UINT32_MAX) return false;
	if (existing == UINT32_MAX && !build_record_reserve_tasks(stream, stream->task_count + 1)) return false;
	if (task.dependencies.count) {
		Bob_Path *dependencies = arena_push_copy_aligned(stream->arena,
			(u64)task.dependencies.count * sizeof(*dependencies), _Alignof(Bob_Path), task.dependencies.items);
		if (!dependencies) return false;
		task.dependencies.items = dependencies;
	}
	if (existing != UINT32_MAX) stream->tasks[existing] = task;
	else stream->tasks[stream->task_count++] = task;
	return true;
}

b32 build_record_remove(Build_Record_Stream *stream, Bob_Path output)
{
	u32 index = build_record_task_index(stream, output);
	if (index == UINT32_MAX) return false;
	if (index + 1 < stream->task_count) memmove(stream->tasks + index, stream->tasks + index + 1,
		(u64)(stream->task_count - index - 1) * sizeof(*stream->tasks));
	--stream->task_count;
	return true;
}

void build_record_replace_tasks(Build_Record_Stream *stream, const Build_Record_Stream *replacement)
{
	stream->tasks = replacement->tasks;
	stream->task_count = replacement->task_count;
	stream->task_capacity = replacement->task_capacity;
}

b32 build_record_stream_init(Build_Record_Stream *stream, Arena *arena, Bob_Build *build)
{
	if (!stream || !arena || !build) return false;
	*stream = (Build_Record_Stream){ .arena = arena, .build = build, .initialized = true };
	return true;
}

void build_record_stream_destroy(Build_Record_Stream *stream)
{
	ASSERT(stream);
	ASSERT(stream->initialized);
	stream->initialized = false;
}

void build_record_stream_clear(Build_Record_Stream *stream)
{
	ASSERT(stream);
	if (!stream->initialized) return;
	build_record_replace_tasks(stream, &(Build_Record_Stream){0});
	stream->paths = NULL;
	stream->path_count = 0;
	stream->path_capacity = 0;
	stream->ids_by_atom = NULL;
	stream->atom_capacity = 0;
}

b32 build_record_stream_snapshot(const Build_Record_Stream *stream, Arena *arena, Build_Record_Snapshot *snapshot)
{
	if (!stream || !stream->initialized || !arena || !snapshot) return false;
	*snapshot = (Build_Record_Snapshot){ .task_count = stream->task_count };
	if (!stream->task_count) return true;
	snapshot->tasks = arena_push_copy_aligned(arena, (u64)stream->task_count * sizeof(*snapshot->tasks),
		_Alignof(Build_Record_Task), stream->tasks);
	return snapshot->tasks != NULL;
}

b32 build_record_snapshot_get(const Build_Record_Snapshot *snapshot, Bob_Path output, Build_Record_Task *result)
{
	if (!snapshot || !result || !bob_path_is_valid(output)) return false;
	for (u32 i = 0; i < snapshot->task_count; ++i) {
		if (snapshot->tasks[i].output.atom.id != output.atom.id) continue;
		*result = snapshot->tasks[i];
		return true;
	}
	*result = (Build_Record_Task){0};
	return false;
}
