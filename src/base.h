#ifndef BASE_H
#define BASE_H

#include "dayan.h"

#include <assert.h>
#include <string.h>

#define ASSERT assert

typedef day_u8  u8;
typedef day_u16 u16;
typedef day_u32 u32;
typedef day_u64 u64;
typedef day_i8  i8;
typedef day_i16 i16;
typedef day_i32 i32;
typedef day_i64 i64;
typedef day_f32 f32;
typedef day_f64 f64;
typedef day_b32 b32;

typedef day_Arena Arena;
typedef day_Scratch Scratch;
typedef day_String String;
typedef day_String_Array String_Array;

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

#define ARRAY_COUNT DAY_ARRAY_COUNT
#define KILOBYTES DAY_KILOBYTES
#define MEGABYTES DAY_MEGABYTES
#define GIGABYTES DAY_GIGABYTES
#define LIT DAY_LIT

#define arena_create day_arena_create
#define arena_set_name day_arena_set_name
#define arena_destroy day_arena_destroy
#define arena_reset day_arena_reset
#define arena_mark day_arena_mark
#define arena_restore day_arena_restore
#define arena_top day_arena_top
#define arena_reserve day_arena_reserve
#define arena_push day_arena_push
#define arena_push_zero day_arena_push_zero
#define arena_push_copy day_arena_push_copy
#define arena_push_data day_arena_push_data
#define arena_push_repeat day_arena_push_nchar
#define arena_appendfv day_arena_pushfv
#define arena_appendf day_arena_pushf

#define begin_scratch day_begin_scratch
#define begin_different_scratch day_begin_different_scratch
#define end_scratch day_end_scratch
#define destroy_global_scratch day_destroy_thread_scratch

#define string_from_data day_string_from_data
#define string_from_range day_string_from_range
#define string_from_cstring day_string_from_cstring
#define string_equal day_string_equal
#define string_slice day_string_slice
#define string_split day_string_split
#define string_split_lines day_string_split_lines
#define string_split_block day_string_split_block
#define string_split_first day_string_split_first
#define string_trim_whitespace day_string_trim_whitespace
#define string_equal_insensitive day_string_equal_insensitive
#define string_starts_with day_string_starts_with
#define string_ends_with day_string_ends_with
#define string_ends_with_insensitive day_string_ends_with_insensitive
#define string_is day_string_is
#define string_count_lines day_string_count_lines

static inline void *arena_push_aligned(Arena *arena, u64 size, u64 alignment)
{
	day_arena_align(arena, alignment);
	return day_arena_push(arena, size);
}

static inline void *arena_push_zero_aligned(Arena *arena, u64 size, u64 alignment)
{
	day_arena_align(arena, alignment);
	return day_arena_push_zero(arena, size);
}

static inline void *arena_push_copy_aligned(Arena *arena, u64 size, u64 alignment, const void *data)
{
	day_arena_align(arena, alignment);
	return day_arena_push_copy(arena, size, data);
}

static inline char *arena_append_str(Arena *arena, String string)
{
	char *result = day_arena_reserve(arena, string.size + 1);
	if (!result) return NULL;
	if (string.size) memcpy(result, string.data, (size_t)string.size);
	result[string.size] = 0;
	arena->used += string.size;
	return result;
}

static inline char *arena_append_text(Arena *arena, const char *text)
{
	return arena_append_str(arena, day_string_from_cstring(text));
}

static inline char *arena_append_char(Arena *arena, char character)
{
	char *result = day_arena_reserve(arena, 2);
	if (!result) return NULL;
	result[0] = character;
	result[1] = 0;
	arena->used += 1;
	return result;
}

static inline String arena_string_from(Arena *arena, void *start)
{
	return day_string_from_range(start, day_arena_top(arena));
}

static inline void arena_finalize_string(Arena *arena, String string)
{
	ASSERT(string.data + string.size == (char *)day_arena_top(arena));
	ASSERT(*(char *)day_arena_top(arena) == 0);
	day_arena_push_char(arena, 0);
}

static inline b32 string_is_terminated(String string)
{
	return string.data && string.data[string.size] == 0;
}

static inline String str_push_copy(Arena *arena, String string)
{
	char *data = day_arena_push_data(arena, string.data, string.size);
	if (!data) return (String){0};
	if (!day_arena_push_char(arena, 0)) return (String){0};
	return day_string_from_data(data, string.size);
}

static inline String arena_push_cstring(Arena *arena, const char *text)
{
	return str_push_copy(arena, day_string_from_cstring(text));
}

#endif
