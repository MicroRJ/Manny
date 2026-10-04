#ifndef BASE_H
#define BASE_H

#include "dy.h"

#include <assert.h>
#include <string.h>

#define ASSERT assert

typedef dy_u8  u8;
typedef dy_u16 u16;
typedef dy_u32 u32;
typedef dy_u64 u64;
typedef dy_i8  i8;
typedef dy_i16 i16;
typedef dy_i32 i32;
typedef dy_i64 i64;
typedef dy_f32 f32;
typedef dy_f64 f64;
typedef dy_b32 b32;

typedef dy_Arena Arena;
typedef dy_Scratch Scratch;
typedef dy_String String;
typedef dy_String_Array String_Array;

#if defined(_MSC_VER)
#define THREAD_LOCAL __declspec(thread)
#else
#define THREAD_LOCAL _Thread_local
#endif

#ifndef true
#define true 1
#endif

#ifndef false
#define false 0
#endif

#define ARRAY_COUNT DY_ARRAY_COUNT
#define KILOBYTES DY_KILOBYTES
#define MEGABYTES DY_MEGABYTES
#define GIGABYTES DY_GIGABYTES
#define LIT DY_LIT

#define arena_create dy_arena_create
#define arena_set_name dy_arena_set_name
#define arena_destroy dy_arena_destroy
#define arena_reset dy_arena_reset
#define arena_mark dy_arena_mark
#define arena_restore dy_arena_restore
#define arena_top dy_arena_top
#define arena_reserve dy_arena_reserve
#define arena_push dy_arena_push
#define arena_push_zero dy_arena_push_zero
#define arena_push_copy dy_arena_push_copy
#define arena_push_data dy_arena_push_data
#define arena_push_repeat dy_arena_push_nchar
#define arena_appendfv dy_arena_pushfv
#define arena_appendf dy_arena_pushf

#define begin_scratch dy_begin_scratch
#define begin_different_scratch dy_begin_different_scratch
#define end_scratch dy_end_scratch
#define destroy_global_scratch dy_destroy_thread_scratch

#define string_from_data dy_string_from_data
#define string_from_range dy_string_from_range
#define string_from_cstring dy_string_from_cstring
#define string_equal dy_string_equal
#define string_slice dy_string_slice
#define string_split dy_string_split
#define string_split_lines dy_string_split_lines
#define string_split_block dy_string_split_block
#define string_split_first dy_string_split_first
#define string_trim_whitespace dy_string_trim_whitespace
#define string_equal_insensitive dy_string_equal_insensitive
#define string_starts_with dy_string_starts_with
#define string_ends_with dy_string_ends_with
#define string_ends_with_insensitive dy_string_ends_with_insensitive
#define string_is dy_string_is
#define string_count_lines dy_string_count_lines

static inline void *arena_push_aligned(Arena *arena, u64 size, u64 alignment)
{
	dy_arena_align(arena, alignment);
	return dy_arena_push(arena, size);
}

static inline void *arena_push_zero_aligned(Arena *arena, u64 size, u64 alignment)
{
	dy_arena_align(arena, alignment);
	return dy_arena_push_zero(arena, size);
}

static inline void *arena_push_copy_aligned(Arena *arena, u64 size, u64 alignment, const void *data)
{
	dy_arena_align(arena, alignment);
	return dy_arena_push_copy(arena, size, data);
}

static inline char *arena_append_str(Arena *arena, String string)
{
	char *result = dy_arena_reserve(arena, string.size + 1);
	if (!result) return NULL;
	if (string.size) memcpy(result, string.data, (size_t)string.size);
	result[string.size] = 0;
	arena->used += string.size;
	return result;
}

static inline char *arena_append_text(Arena *arena, const char *text)
{
	return arena_append_str(arena, dy_string_from_cstring(text));
}

static inline char *arena_append_char(Arena *arena, char character)
{
	char *result = dy_arena_reserve(arena, 2);
	if (!result) return NULL;
	result[0] = character;
	result[1] = 0;
	arena->used += 1;
	return result;
}

static inline String arena_string_from(Arena *arena, void *start)
{
	return dy_string_from_range(start, dy_arena_top(arena));
}

static inline void arena_finalize_string(Arena *arena, String string)
{
	ASSERT(string.data + string.size == (char *)dy_arena_top(arena));
	ASSERT(*(char *)dy_arena_top(arena) == 0);
	dy_arena_push_char(arena, 0);
}

static inline b32 string_is_terminated(String string)
{
	return string.data && string.data[string.size] == 0;
}

static inline String str_push_copy(Arena *arena, String string)
{
	char *data = dy_arena_push_data(arena, string.data, string.size);
	if (!data) return (String){0};
	if (!dy_arena_push_char(arena, 0)) return (String){0};
	return dy_string_from_data(data, string.size);
}

static inline String arena_push_cstring(Arena *arena, const char *text)
{
	return str_push_copy(arena, dy_string_from_cstring(text));
}

#endif
