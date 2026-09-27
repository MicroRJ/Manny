#include "build_record_stream.h"
#include "build_record_internal.h"
#include "platform_adapter.h"
#include "platform.h"

#include <string.h>

#define BUILD_RECORD_STREAM_VERSION            3
#define BUILD_RECORD_STREAM_MAGIC              "MNYSTATE"
#define BUILD_RECORD_STREAM_MAGIC_SIZE         8
#define BUILD_RECORD_STREAM_HEADER_SIZE        16
#define BUILD_RECORD_STREAM_RECORD_HEADER_SIZE 8

typedef enum Build_Record_Op
{
	BUILD_RECORD_OP_INTERN = 1,
	BUILD_RECORD_OP_SET,
	BUILD_RECORD_OP_REMOVE,
}
Build_Record_Op;

typedef struct
{
	u8  *data;
	u64  size;
	u64  cursor;
}
Build_Record_Encoder;

typedef struct Build_Record_Decoder
{
	const u8 *data;
	u64       size;
	u64       cursor;
}
Build_Record_Decoder;

typedef u32 Build_Record_Path_Id;

#define BUILD_RECORD_PATH_ID_NONE            ((Build_Record_Path_Id)0)
#define BUILD_RECORD_PATH_INITIAL_CAPACITY 16

static b32 build_record_stream_is_valid(const Build_Record_Stream *stream)
{
	return stream && stream->arena && stream->build && stream->initialized;
}

static void build_record_stream_replace_paths(Build_Record_Stream *stream, const Build_Record_Stream *replacement)
{
	stream->paths           = replacement->paths;
	stream->path_count      = replacement->path_count;
	stream->path_capacity   = replacement->path_capacity;
	stream->ids_by_atom     = replacement->ids_by_atom;
	stream->atom_capacity   = replacement->atom_capacity;
}

static b32 build_record_stream_reserve_paths(Arena *arena, Build_Record_Stream *stream, u32 needed)
{
	if (stream->path_capacity >= needed) return true;
	u32 capacity = stream->path_capacity ? stream->path_capacity : BUILD_RECORD_PATH_INITIAL_CAPACITY;
	while (capacity < needed) {
		if (capacity > UINT32_MAX / 2) return false;
		capacity *= 2;
	}
	Manny_Path *paths = arena_push_zero_aligned(arena, (u64)capacity * sizeof(*paths), _Alignof(Manny_Path));
	if (!paths) return false;
	if (stream->path_count) memcpy(paths, stream->paths, (u64)stream->path_count * sizeof(*stream->paths));
	stream->paths = paths;
	stream->path_capacity = capacity;
	return true;
}

static b32 build_record_stream_reserve_atoms(Arena *arena, Build_Record_Stream *stream, u32 atom_id)
{
	if (atom_id < stream->atom_capacity) return true;
	u32 capacity = stream->atom_capacity ? stream->atom_capacity : BUILD_RECORD_PATH_INITIAL_CAPACITY;
	while (capacity <= atom_id) {
		if (capacity > UINT32_MAX / 2) return false;
		capacity *= 2;
	}
	u32 *ids = arena_push_zero_aligned(arena, (u64)capacity * sizeof(*ids), _Alignof(u32));
	if (!ids) return false;
	if (stream->atom_capacity) memcpy(ids, stream->ids_by_atom, (u64)stream->atom_capacity * sizeof(*stream->ids_by_atom));
	stream->ids_by_atom = ids;
	stream->atom_capacity = capacity;
	return true;
}

static Build_Record_Path_Id build_record_stream_path_id(const Build_Record_Stream *stream, Manny_Path path)
{
	u32 atom = path.atom.id;
	if (!stream || !manny_path_is_valid(path) || atom >= stream->atom_capacity) return BUILD_RECORD_PATH_ID_NONE;
	return stream->ids_by_atom[atom];
}

static Manny_Path build_record_stream_path(const Build_Record_Stream *stream, Build_Record_Path_Id id)
{
	if (!stream || id == BUILD_RECORD_PATH_ID_NONE || id > stream->path_count) return (Manny_Path){0};
	return stream->paths[id - 1];
}

static Build_Record_Path_Id build_record_stream_add_path(Arena *arena, Build_Record_Stream *stream, Manny_Path path)
{
	Build_Record_Path_Id existing = build_record_stream_path_id(stream, path);
	if (existing != BUILD_RECORD_PATH_ID_NONE) return existing;
	if (!arena || !stream || !manny_path_is_valid(path) || stream->path_count == UINT32_MAX) return BUILD_RECORD_PATH_ID_NONE;
	if (!build_record_stream_reserve_paths(arena, stream, stream->path_count + 1)) return BUILD_RECORD_PATH_ID_NONE;
	if (!build_record_stream_reserve_atoms(arena, stream, path.atom.id)) return BUILD_RECORD_PATH_ID_NONE;
	Build_Record_Path_Id id = ++stream->path_count;
	stream->paths[id - 1] = path;
	stream->ids_by_atom[path.atom.id] = id;
	return id;
}

static b32 build_record_stream_add_replayed_path(Arena *arena, Build_Record_Stream *stream, Manny_Path path)
{
	if (!arena || !stream || !manny_path_is_valid(path) || stream->path_count == UINT32_MAX) return false;
	if (!build_record_stream_reserve_paths(arena, stream, stream->path_count + 1)) return false;
	if (!build_record_stream_reserve_atoms(arena, stream, path.atom.id)) return false;
	Build_Record_Path_Id id = ++stream->path_count;
	stream->paths[id - 1] = path;
	stream->ids_by_atom[path.atom.id] = id;
	return true;
}

static b32 build_record_size_add(u64 *total, u64 count, u64 size)
{
	if (!total || (count && size > UINT64_MAX / count)) return false;
	u64 addition = count * size;
	if (*total > UINT64_MAX - addition) return false;
	*total += addition;
	return true;
}

static b32 build_record_encode_bytes(Build_Record_Encoder *encoder, const void *data, u64 size)
{
	if (!encoder || (!data && size)) return false;
	if (encoder->cursor > encoder->size) return false;
	if (size > encoder->size - encoder->cursor) return false;
	if (size) memcpy(encoder->data + encoder->cursor, data, (size_t)size);
	encoder->cursor += size;
	return true;
}

static b32 build_record_encode_u32(Build_Record_Encoder *encoder, u32 value)
{
	u8 bytes[4] = {
		(u8)(value >> 0),
		(u8)(value >> 8),
		(u8)(value >> 16),
		(u8)(value >> 24),
	};
	return build_record_encode_bytes(encoder, bytes, sizeof(bytes));
}

static b32 build_record_encode_u64(Build_Record_Encoder *encoder, u64 value)
{
	u8 bytes[8] = {
		(u8)(value >> 0),
		(u8)(value >> 8),
		(u8)(value >> 16),
		(u8)(value >> 24),
		(u8)(value >> 32),
		(u8)(value >> 40),
		(u8)(value >> 48),
		(u8)(value >> 56),
	};
	return build_record_encode_bytes(encoder, bytes, sizeof(bytes));
}

static b32 build_record_decode_bytes(Build_Record_Decoder *decoder, const u8 **data, u64 size)
{
	if (!decoder || !data) return false;
	if (decoder->cursor > decoder->size) return false;
	if (size > decoder->size - decoder->cursor) return false;
	*data = decoder->data + decoder->cursor;
	decoder->cursor += size;
	return true;
}

static b32 build_record_decode_u32(Build_Record_Decoder *decoder, u32 *value)
{
	const u8 *bytes;
	if (!value || !build_record_decode_bytes(decoder, &bytes, 4)) return false;
	*value = (u32)bytes[0] | ((u32)bytes[1] << 8) | ((u32)bytes[2] << 16) | ((u32)bytes[3] << 24);
	return true;
}

static b32 build_record_decode_u64(Build_Record_Decoder *decoder, u64 *value)
{
	const u8 *bytes;
	if (!value || !build_record_decode_bytes(decoder, &bytes, 8)) return false;
	*value = (u64)bytes[0] | ((u64)bytes[1] << 8) | ((u64)bytes[2] << 16) | ((u64)bytes[3] << 24) | ((u64)bytes[4] << 32) | ((u64)bytes[5] << 40) | ((u64)bytes[6] << 48) | ((u64)bytes[7] << 56);
	return true;
}

static u32 build_record_crc32c(const void *data, u64 size)
{
	const u8 *bytes = data;
	u32 crc = UINT32_MAX;
	for (u64 i = 0; i < size; ++i) {
		crc ^= bytes[i];
		for (u32 bit = 0; bit < 8; ++bit) {
			u32 mask = 0U - (crc & 1U);
			crc = (crc >> 1) ^ (0x82F63B78U & mask);
		}
	}
	return ~crc;
}

static b32 build_record_stream_size(const Build_Record_Stream *stream, const Build_Record_Stream *path_index, u64 *stream_size)
{
	u64 size = BUILD_RECORD_STREAM_HEADER_SIZE;
	if (!stream || !stream->build || !path_index || !stream_size) return false;
	if (path_index->path_count && !path_index->paths) return false;
	if (stream->task_count && !stream->tasks) return false;

	for (u32 i = 0; i < path_index->path_count; ++i) {
		Manny_Path handle = path_index->paths[i];
		String path = manny_path_string(stream->build, handle);
		u64 content_size = 8;
		if (!path.data || path.size == 0 || path.size > UINT32_MAX) return false;
		if (!manny_path_is_valid(handle)) return false;
		if (!build_record_size_add(&content_size, 1, path.size)) return false;
		if (content_size > UINT32_MAX) return false;
		if (!build_record_size_add(&size, 1, BUILD_RECORD_STREAM_RECORD_HEADER_SIZE)) return false;
		if (!build_record_size_add(&size, 1, content_size)) return false;
	}

	for (u32 i = 0; i < stream->task_count; ++i) {
		const Build_Record_Task *task = stream->tasks + i;
		u64 content_size = 20 + MANNY_FINGERPRINT_SIZE;
		if (build_record_stream_path_id(path_index, task->output) == BUILD_RECORD_PATH_ID_NONE) return false;
		if (build_record_task_index(stream, task->output) != i) return false;
		if (task->dependencies.count && !task->dependencies.items) return false;
		if (!build_record_size_add(&content_size, task->dependencies.count, 4)) return false;
		if (content_size > UINT32_MAX) return false;
		for (u32 dependency = 0; dependency < task->dependencies.count; ++dependency) {
			if (build_record_stream_path_id(path_index, task->dependencies.items[dependency]) == BUILD_RECORD_PATH_ID_NONE) return false;
		}
		if (!build_record_size_add(&size, 1, BUILD_RECORD_STREAM_RECORD_HEADER_SIZE)) return false;
		if (!build_record_size_add(&size, 1, content_size)) return false;
	}

	*stream_size = size;
	return true;
}

static b32 build_record_stream_begin_record(Build_Record_Encoder *stream, u32 content_size, Build_Record_Encoder *content, u64 *checksum_offset)
{
	if (!stream || !content || !checksum_offset) return false;
	if (!build_record_encode_u32(stream, content_size)) return false;
	*checksum_offset = stream->cursor;
	if (!build_record_encode_u32(stream, 0)) return false;
	if (stream->cursor > stream->size || content_size > stream->size - stream->cursor) return false;
	*content = (Build_Record_Encoder){ stream->data + stream->cursor, content_size, 0 };
	stream->cursor += content_size;
	return true;
}

static b32 build_record_stream_finish_record(Build_Record_Encoder *stream, const Build_Record_Encoder *content, u64 checksum_offset)
{
	Build_Record_Encoder checksum;
	if (!stream || !content || content->cursor != content->size) return false;
	if (checksum_offset > stream->size || sizeof(u32) > stream->size - checksum_offset) return false;
	checksum = (Build_Record_Encoder){ stream->data + checksum_offset, sizeof(u32), 0 };
	return build_record_encode_u32(&checksum, build_record_crc32c(content->data, content->size));
}

static b32 build_record_stream_encode_intern(Build_Record_Encoder *encoder, String path)
{
	u64 checksum_offset;
	Build_Record_Encoder content;
	if (!encoder || !path.data || path.size == 0 || path.size > UINT32_MAX - 8) return false;
	if (!build_record_stream_begin_record(encoder, 8 + (u32)path.size, &content, &checksum_offset)) return false;
	if (!build_record_encode_u32(&content, BUILD_RECORD_OP_INTERN)) return false;
	if (!build_record_encode_u32(&content, (u32)path.size)) return false;
	if (!build_record_encode_bytes(&content, path.data, path.size)) return false;
	return build_record_stream_finish_record(encoder, &content, checksum_offset);
}

static b32 build_record_stream_encode_set(Build_Record_Encoder *encoder, const Build_Record_Stream *state_stream, const Build_Record_Task *task)
{
	u64 content_size = 20 + MANNY_FINGERPRINT_SIZE;
	u64 checksum_offset;
	Build_Record_Encoder content;
	if (!encoder || !state_stream || !task || (task->dependencies.count && !task->dependencies.items)) return false;
	if (!build_record_size_add(&content_size, task->dependencies.count, 4) || content_size > UINT32_MAX) return false;
	if (!build_record_stream_begin_record(encoder, (u32)content_size, &content, &checksum_offset)) return false;
	if (!build_record_encode_u32(&content, BUILD_RECORD_OP_SET)) return false;
	if (!build_record_encode_u32(&content, build_record_stream_path_id(state_stream, task->output))) return false;
	if (!build_record_encode_u64(&content, task->output_stamp)) return false;
	if (!build_record_encode_bytes(&content, task->fingerprint.bytes, sizeof(task->fingerprint.bytes))) return false;
	if (!build_record_encode_u32(&content, task->dependencies.count)) return false;
	for (u32 dependency = 0; dependency < task->dependencies.count; ++dependency) {
		if (!build_record_encode_u32(&content, build_record_stream_path_id(state_stream, task->dependencies.items[dependency]))) return false;
	}
	return build_record_stream_finish_record(encoder, &content, checksum_offset);
}

static b32 build_record_stream_encode_remove(Build_Record_Encoder *encoder, Build_Record_Path_Id output)
{
	u64 checksum_offset;
	Build_Record_Encoder content;
	if (!encoder || output == BUILD_RECORD_PATH_ID_NONE) return false;
	if (!build_record_stream_begin_record(encoder, 8, &content, &checksum_offset)) return false;
	if (!build_record_encode_u32(&content, BUILD_RECORD_OP_REMOVE)) return false;
	if (!build_record_encode_u32(&content, output)) return false;
	return build_record_stream_finish_record(encoder, &content, checksum_offset);
}

static b32 build_record_stream_encode_with_paths(Arena *arena, const Build_Record_Stream *stream,
	const Build_Record_Stream *path_index, String *encoded)
{
	u64 mark;
	u64 stream_size;
	Build_Record_Encoder encoder = {0};
	if (!arena || !stream || !path_index || !encoded) return false;
	*encoded = (String){0};
	if (!build_record_stream_size(stream, path_index, &stream_size) || stream_size > SIZE_MAX) return false;
	mark = arena_mark(arena);
	encoder.data = arena_push(arena, stream_size);
	encoder.size = stream_size;
	if (!encoder.data) return false;

	if (!build_record_encode_bytes(&encoder, BUILD_RECORD_STREAM_MAGIC, BUILD_RECORD_STREAM_MAGIC_SIZE)) goto failure;
	if (!build_record_encode_u32(&encoder, BUILD_RECORD_STREAM_VERSION)) goto failure;
	if (!build_record_encode_u32(&encoder, BUILD_RECORD_STREAM_HEADER_SIZE)) goto failure;

	for (u32 i = 0; i < path_index->path_count; ++i) {
		if (!build_record_stream_encode_intern(&encoder, manny_path_string(stream->build, path_index->paths[i]))) goto failure;
	}

	for (u32 i = 0; i < stream->task_count; ++i) {
		if (!build_record_stream_encode_set(&encoder, path_index, stream->tasks + i)) goto failure;
	}

	if (encoder.cursor != encoder.size) goto failure;
	*encoded = string_from_data(encoder.data, encoder.size);
	return true;

	failure:
	arena_restore(arena, mark);
	return false;
}

static Build_Record_Result build_record_stream_replay_encoded(Build_Record_Stream *stream, String encoded)
{
	u64 mark;
	Build_Record_Stream decoded = {
		.arena = stream ? stream->arena : NULL,
		.build = stream ? stream->build : NULL,
		.initialized = true,
	};
	Build_Record_Decoder decoder = { (const u8 *)encoded.data, encoded.size, 0 };
	Arena *arena = stream ? stream->arena : NULL;
	Manny_Build *build = stream ? stream->build : NULL;
	const u8 *magic;
	u32 version;
	u32 header_size;
	if (!build_record_stream_is_valid(stream)) return BUILD_RECORD_ERROR;
	build_record_replace_tasks(stream, &(Build_Record_Stream){0});
	build_record_stream_replace_paths(stream, &(Build_Record_Stream){0});
	if (!encoded.data || encoded.size < BUILD_RECORD_STREAM_HEADER_SIZE) return BUILD_RECORD_INVALID;
	mark = arena_mark(arena);

	if (!build_record_decode_bytes(&decoder, &magic, BUILD_RECORD_STREAM_MAGIC_SIZE)) goto invalid;
	if (memcmp(magic, BUILD_RECORD_STREAM_MAGIC, BUILD_RECORD_STREAM_MAGIC_SIZE) != 0) goto invalid;
	if (!build_record_decode_u32(&decoder, &version)) goto invalid;
	if (!build_record_decode_u32(&decoder, &header_size)) goto invalid;
	if (version != BUILD_RECORD_STREAM_VERSION) goto invalid;
	if (header_size != BUILD_RECORD_STREAM_HEADER_SIZE) goto invalid;

	while (decoder.cursor < decoder.size) {
		u32 content_size;
		u32 checksum;
		const u8 *content_data;
		Build_Record_Decoder content;
		u32 operation;

		if (decoder.size - decoder.cursor < BUILD_RECORD_STREAM_RECORD_HEADER_SIZE) goto truncated;
		if (!build_record_decode_u32(&decoder, &content_size)) goto truncated;
		if (!build_record_decode_u32(&decoder, &checksum)) goto truncated;
		if (content_size > decoder.size - decoder.cursor) goto truncated;
		if (content_size < sizeof(u32)) goto invalid;
		if (!build_record_decode_bytes(&decoder, &content_data, content_size)) goto truncated;
		if (build_record_crc32c(content_data, content_size) != checksum) goto invalid;
		content = (Build_Record_Decoder){ content_data, content_size, 0 };
		if (!build_record_decode_u32(&content, &operation)) goto invalid;

		switch ((Build_Record_Op)operation) {

			case BUILD_RECORD_OP_INTERN:
			{
				u32       path_size;
				const u8 *path_data;
				Manny_Path  path;
				if (!build_record_decode_u32(&content, &path_size)) goto invalid;
				if (path_size == 0 || path_size != content.size - content.cursor) goto invalid;
				if (!build_record_decode_bytes(&content, &path_data, path_size)) goto invalid;
				if (!manny_path_resolve(build, manny_build_root(build), string_from_data((void *)path_data, path_size), &path)) goto error;
				if (build_record_stream_path_id(&decoded, path) != BUILD_RECORD_PATH_ID_NONE) goto invalid;
				if (!build_record_stream_add_replayed_path(arena, &decoded, path)) goto error;

			} break;

			case BUILD_RECORD_OP_SET:
			{
				u32       output;
				u64       output_stamp;
				const u8 *fingerprint;
				u32       dependency_count;

				if (!build_record_decode_u32(&content, &output)) goto invalid;
				if (!build_record_decode_u64(&content, &output_stamp)) goto invalid;
				if (!build_record_decode_bytes(&content, &fingerprint, MANNY_FINGERPRINT_SIZE)) goto invalid;
				if (!build_record_decode_u32(&content, &dependency_count)) goto invalid;

				if ((u64)dependency_count * 4 != content.size - content.cursor) goto invalid;
				if (output == BUILD_RECORD_PATH_ID_NONE || output > decoded.path_count) goto invalid;
				Manny_Path output_path = build_record_stream_path(&decoded, output);

				u32 existing = build_record_task_index(&decoded, output_path);
				if (existing == UINT32_MAX && decoded.task_count == UINT32_MAX) goto error;
				if (existing == UINT32_MAX && !build_record_reserve_tasks(&decoded, decoded.task_count + 1)) goto error;

				Build_Record_Task task = {0};
				task.output = output_path;
				task.output_stamp = output_stamp;
				memcpy(task.fingerprint.bytes, fingerprint, sizeof(task.fingerprint.bytes));

				if (dependency_count) {
					task.dependencies.items = arena_push_zero_aligned(arena, (u64)dependency_count * sizeof(*task.dependencies.items), _Alignof(Manny_Path));
					if (!task.dependencies.items) goto error;
				}
				for (u32 dependency = 0; dependency < dependency_count; ++dependency) {
					u32 id;
					if (!build_record_decode_u32(&content, &id)) goto invalid;
					if (id == BUILD_RECORD_PATH_ID_NONE || id > decoded.path_count) goto invalid;
					task.dependencies.items[task.dependencies.count++] = build_record_stream_path(&decoded, id);
				}

				// TODO(RJ): we may want to log this!
				if (existing == UINT32_MAX) decoded.tasks[decoded.task_count++] = task;
				else decoded.tasks[existing] = task;

			} break;

			case BUILD_RECORD_OP_REMOVE: {
				u32 output;
				if (!build_record_decode_u32(&content, &output)) goto invalid;
				if (content.cursor != content.size) goto invalid;
				if (output == BUILD_RECORD_PATH_ID_NONE || output > decoded.path_count) goto invalid;
				u32 index = build_record_task_index(&decoded, build_record_stream_path(&decoded, output));
				if (index != UINT32_MAX) {
					if (index + 1 < decoded.task_count) memmove(decoded.tasks + index, decoded.tasks + index + 1, (u64)(decoded.task_count - index - 1) * sizeof(*decoded.tasks));
					--decoded.task_count;
				}
			} break;

			default: goto invalid;
		}

		if (content.cursor != content.size) goto invalid;
	}

	build_record_replace_tasks(stream, &decoded);
	build_record_stream_replace_paths(stream, &decoded);
	return BUILD_RECORD_OK;

	truncated:
	build_record_replace_tasks(stream, &decoded);
	build_record_stream_replace_paths(stream, &decoded);
	return BUILD_RECORD_RECOVERED;

	invalid:
	arena_restore(arena, mark);
	build_record_replace_tasks(stream, &(Build_Record_Stream){0});
	build_record_stream_replace_paths(stream, &(Build_Record_Stream){0});
	return BUILD_RECORD_INVALID;

	error:
	arena_restore(arena, mark);
	build_record_replace_tasks(stream, &(Build_Record_Stream){0});
	build_record_stream_replace_paths(stream, &(Build_Record_Stream){0});
	return BUILD_RECORD_ERROR;
}

static b32 build_record_stream_append_bytes(String path, const void *data, u64 size)
{
	Platform_File file;
	u64 written = 0;
	b32 result;
	if (!string_is_terminated(path) || (!data && size)) return false;
	file = platform_access_file(path.data, PLATFORM_FILE_OPEN_EXISTING, PLATFORM_FILE_WRITE | PLATFORM_FILE_SHARE_READ);
	if (!platform_file_is_valid(file)) return false;
	result = platform_set_file_cursor(file, PLATFORM_SEEK_END, 0, NULL) && platform_write_file(file, data, size, &written) && written == size;
	platform_close_file(file);
	return result;
}

static void build_record_stream_rollback_paths(Build_Record_Stream *stream,
	const Build_Record_Stream *previous, u32 first_new_path, u64 arena_mark)
{
	for (u32 i = first_new_path; i < stream->path_count; ++i) {
		u32 atom = stream->paths[i].atom.id;
		if (atom < previous->atom_capacity) previous->ids_by_atom[atom] = BUILD_RECORD_PATH_ID_NONE;
	}
	build_record_stream_replace_paths(stream, previous);
	arena_restore(stream->arena, arena_mark);
}

b32 build_record_stream_append_set(Build_Record_Stream *stream, String path, Build_Record_Task task)
{
	Build_Record_Stream previous;
	u32 first_new_path;
	u64 mark;
	u64 append_size = 0;
	Scratch scratch = {0};
	Build_Record_Encoder encoder = {0};
	if (!build_record_stream_is_valid(stream) || !string_is_terminated(path) || path.size == 0) return false;
	if (!manny_path_is_valid(task.output) || (task.dependencies.count && !task.dependencies.items)) return false;
	previous = *stream;
	first_new_path = stream->path_count;
	mark = arena_mark(stream->arena);
	if (build_record_stream_add_path(stream->arena, stream, task.output) == BUILD_RECORD_PATH_ID_NONE) goto rollback;
	for (u32 i = 0; i < task.dependencies.count; ++i) {
		if (build_record_stream_add_path(stream->arena, stream, task.dependencies.items[i]) == BUILD_RECORD_PATH_ID_NONE) goto rollback;
	}
	for (u32 i = first_new_path; i < stream->path_count; ++i) {
		String new_path = manny_path_string(stream->build, stream->paths[i]);
		if (!build_record_size_add(&append_size, 1, BUILD_RECORD_STREAM_RECORD_HEADER_SIZE + 8)) goto rollback;
		if (!build_record_size_add(&append_size, 1, new_path.size)) goto rollback;
	}
	if (!build_record_size_add(&append_size, 1, BUILD_RECORD_STREAM_RECORD_HEADER_SIZE + 20 + MANNY_FINGERPRINT_SIZE)) goto rollback;
	if (!build_record_size_add(&append_size, task.dependencies.count, 4) || append_size > SIZE_MAX) goto rollback;

	scratch = begin_different_scratch(stream->arena);
	encoder.data = arena_push(scratch.arena, append_size);
	encoder.size = append_size;
	if (!encoder.data) goto failure;
	for (u32 i = first_new_path; i < stream->path_count; ++i) {
		if (!build_record_stream_encode_intern(&encoder, manny_path_string(stream->build, stream->paths[i]))) goto failure;
	}
	if (!build_record_stream_encode_set(&encoder, stream, &task)) goto failure;
	if (encoder.cursor != encoder.size || !build_record_stream_append_bytes(path, encoder.data, encoder.size)) goto failure;
	end_scratch(scratch);
	return build_record_set(stream, task);

	failure:
	end_scratch(scratch);
	rollback:
	build_record_stream_rollback_paths(stream, &previous, first_new_path, mark);
	return false;
}

b32 build_record_stream_append_remove(Build_Record_Stream *stream, String path, Manny_Path output)
{
	u8 bytes[BUILD_RECORD_STREAM_RECORD_HEADER_SIZE + 8];
	Build_Record_Encoder encoder = { bytes, sizeof(bytes), 0 };
	if (!build_record_stream_is_valid(stream) || !string_is_terminated(path) || path.size == 0) return false;
	if (build_record_task_index(stream, output) == UINT32_MAX) return true;
	Build_Record_Path_Id output_id = build_record_stream_path_id(stream, output);
	if (!build_record_stream_encode_remove(&encoder, output_id) || encoder.cursor != encoder.size) return false;
	if (!build_record_stream_append_bytes(path, encoder.data, encoder.size)) return false;
	return build_record_remove(stream, output);
}

static String build_record_parent_directory(String path)
{
	for (u64 i = path.size; i > 0; --i) {
		u64 separator = i - 1;
		if (path.data[separator] != '/' && path.data[separator] != '\\') continue;
		if (separator == 0 || (separator == 2 && path.data[1] == ':')) ++separator;
		return string_slice(path, 0, separator);
	}
	return (String){0};
}

static b32 build_record_stream_collect_paths(Arena *arena, const Build_Record_Stream *stream, Build_Record_Stream *path_index)
{
	if (!arena || !stream || !path_index) return false;
	for (u32 i = 0; i < stream->task_count; ++i) {
		const Build_Record_Task *task = stream->tasks + i;
		if (build_record_stream_add_path(arena, path_index, task->output) == BUILD_RECORD_PATH_ID_NONE) return false;
		for (u32 dependency = 0; dependency < task->dependencies.count; ++dependency) {
			if (build_record_stream_add_path(arena, path_index, task->dependencies.items[dependency]) == BUILD_RECORD_PATH_ID_NONE) return false;
		}
	}
	return true;
}

b32 build_record_stream_compact(Build_Record_Stream *stream, String path)
{
	u64 mark;
	u64 stream_size;
	u64 arena_capacity = 64;
	Arena arena = {0};
	Build_Record_Stream compacted = {
		.arena = stream ? stream->arena : NULL,
		.build = stream ? stream->build : NULL,
	};
	String parent;
	String temporary = {0};
	String encoded = {0};
	b32 result = false;

	if (!build_record_stream_is_valid(stream) || !string_is_terminated(path) || path.size == 0) return false;
	mark = arena_mark(stream->arena);
	if (!build_record_stream_collect_paths(stream->arena, stream, &compacted)) goto done;
	if (!build_record_stream_size(stream, &compacted, &stream_size) || stream_size > SIZE_MAX) goto done;
	if (!build_record_size_add(&arena_capacity, 1, stream_size)) goto done;
	if (!build_record_size_add(&arena_capacity, 3, path.size)) goto done;
	arena = arena_create(arena_capacity);
	arena_set_name(&arena, "build state stream");
	if (!arena.data) goto done;
	parent = build_record_parent_directory(path);
	if (parent.size) {
		parent = str_push_copy(&arena, parent);
		if (!parent.data || !platform_create_directories(parent.data)) goto done;
	}
	{
		void *start = arena_top(&arena);
		arena_append_str(&arena, path);
		arena_append_text(&arena, ".tmp");
		temporary = arena_string_from(&arena, start);
		arena_finalize_string(&arena, temporary);
	}
	if (!build_record_stream_encode_with_paths(&arena, stream, &compacted, &encoded)) goto done;
	if (!manny_platform_write_entire_file(temporary, encoded.data, (size_t)encoded.size)) goto done;
	if (!platform_move_file(temporary.data, path.data, true)) goto done;
	result = true;

	done:
	if (!result && temporary.data) platform_remove_file(temporary.data);
	if (result) build_record_stream_replace_paths(stream, &compacted);
	else arena_restore(stream->arena, mark);
	arena_destroy(&arena);
	return result;
}

Build_Record_Result build_record_stream_load(Build_Record_Stream *stream, String path)
{
	if (!build_record_stream_is_valid(stream) || !string_is_terminated(path) || path.size == 0) return BUILD_RECORD_ERROR;
	build_record_replace_tasks(stream, &(Build_Record_Stream){0});
	build_record_stream_replace_paths(stream, &(Build_Record_Stream){0});

	Manny_Platform_File_Info info;
	if (!manny_platform_file_info(path, &info)) return BUILD_RECORD_MISSING;
	if (info.size == UINT64_MAX) return BUILD_RECORD_ERROR;

	// NOTE(RJ): switched to using scratch arena instead!
	// TODO(RJ): ensure scratch arena has enough capacity, otherwise allocate a new one!
	Scratch scratch = begin_scratch();
	if (!scratch.arena->data) return BUILD_RECORD_ERROR;

	String source;
	if (!manny_platform_read_entire_file(scratch.arena, path, &source)) {
		end_scratch(scratch);
		return BUILD_RECORD_ERROR;
	}

	Build_Record_Result result = build_record_stream_replay_encoded(stream, source);
	end_scratch(scratch);
	return result;
}
