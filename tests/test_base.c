#include "test.h"

static b32 environment_equals(const char *name, const char *expected)
{
    Scratch scratch = begin_scratch();
    String value = {0};
	b32 equal = manny_platform_get_environment(string_from_cstring(name), scratch.arena, &value) &&
        string_equal(value, string_from_cstring(expected));
    end_scratch(scratch);
    return equal;
}

static b32 test_vcvars_cache_application(void)
{
    static const char cache_text[] =
        "MANNY_VCVARS_CACHE_V1\n"
        "prepend MANNY_VCVARS_TEST_PREPEND=tool;\n"
        "append MANNY_VCVARS_TEST_APPEND=;tail\n"
        "set MANNY_VCVARS_TEST_SET=value\n";
    b32 result = false;

	manny_platform_set_environment(LIT("MANNY_VCVARS_TEST_PREPEND"), LIT("base"));
	manny_platform_set_environment(LIT("MANNY_VCVARS_TEST_APPEND"), LIT("base"));
	manny_platform_set_environment(LIT("MANNY_VCVARS_TEST_SET"), (String){0});

    if (!vcvars_cache_apply(string_from_cstring(cache_text))) goto cleanup;
    if (!environment_equals("MANNY_VCVARS_TEST_PREPEND", "tool;base")) goto cleanup;
    if (!environment_equals("MANNY_VCVARS_TEST_APPEND", "base;tail")) goto cleanup;
    if (!environment_equals("MANNY_VCVARS_TEST_SET", "value")) goto cleanup;
    result = true;

cleanup:
	manny_platform_set_environment(LIT("MANNY_VCVARS_TEST_PREPEND"), (String){0});
	manny_platform_set_environment(LIT("MANNY_VCVARS_TEST_APPEND"), (String){0});
	manny_platform_set_environment(LIT("MANNY_VCVARS_TEST_SET"), (String){0});
    return result;
}

static b32 test_high_resolution_timer(void)
{
    u64 frequency = dy_counter_frequency();
    u64 before = dy_counter();
    Sleep(1);
    return frequency > 0 && dy_counter() >= before && dy_current_thread_id() != 0;
}

static b32 test_blake3(void)
{
	static const u8 empty_hash[BLAKE3_OUT_LEN] = {
		0xaf, 0x13, 0x49, 0xb9, 0xf5, 0xf9, 0xa1, 0xa6,
		0xa0, 0x40, 0x4d, 0xea, 0x36, 0xdc, 0xc9, 0x49,
		0x9b, 0xcb, 0x25, 0xc9, 0xad, 0xc1, 0x12, 0xb7,
		0xcc, 0x9a, 0x93, 0xca, 0xe4, 0x1f, 0x32, 0x62,
	};
	static const u8 abc_hash[BLAKE3_OUT_LEN] = {
		0x64, 0x37, 0xb3, 0xac, 0x38, 0x46, 0x51, 0x33,
		0xff, 0xb6, 0x3b, 0x75, 0x27, 0x3a, 0x8d, 0xb5,
		0x48, 0xc5, 0x58, 0x46, 0x5d, 0x79, 0xdb, 0x03,
		0xfd, 0x35, 0x9c, 0x6c, 0xd5, 0xbd, 0x9d, 0x85,
	};
	blake3_hasher hasher;
	u8 hash[BLAKE3_OUT_LEN];

	blake3_hasher_init(&hasher);
	blake3_hasher_finalize(&hasher, hash, sizeof(hash));
	CHECK(memcmp(hash, empty_hash, sizeof(hash)) == 0);

	blake3_hasher_init(&hasher);
	blake3_hasher_update(&hasher, "a", 1);
	blake3_hasher_update(&hasher, "bc", 2);
	blake3_hasher_finalize(&hasher, hash, sizeof(hash));
	CHECK(memcmp(hash, abc_hash, sizeof(hash)) == 0);
	return true;
}

static b32 test_build_paths(void)
{
	Manny_Build *build = manny_build_create();
	Manny_Build *rooted;
	Manny *graph;
	Manny_Execution *execution = NULL;
	Manny_Path root;
	Manny_Path first;
	Manny_Path second;
	Manny_Path absolute;
	Manny_Path expected_root;
	String root_string;
	String first_string;
	CHECK(build != NULL);
	graph = manny_build_graph(build);
	CHECK(graph != NULL);

	root = manny_build_root(build);
	root_string = manny_path_string(build, root);
	CHECK(manny_path_is_valid(root));
	CHECK(root_string.size >= 3 && root_string.data[1] == ':');
	CHECK(root_string.data[0] >= 'A' && root_string.data[0] <= 'Z');
	CHECK(memchr(root_string.data, '\\', (size_t)root_string.size) == NULL);
	CHECK(manny_path_resolve(build, root, LIT("build\\path-test\\temporary\\..\\file.obj"), &first));
	CHECK(manny_path_resolve(build, root, LIT(".\\build/path-test/file.obj"), &second));
	CHECK(first.atom.id == second.atom.id);
	first_string = manny_path_string(build, first);
	CHECK(memchr(first_string.data, '\\', (size_t)first_string.size) == NULL);
	CHECK(manny_path_resolve(build, root, first_string, &absolute));
	CHECK(absolute.atom.id == first.atom.id);
	CHECK(!manny_path_resolve(build, root, LIT(""), &absolute));
	CHECK(manny_path_resolve(build, root, LIT("build"), &expected_root));
	rooted = manny_build_create_at(LIT("build"));
	CHECK(rooted != NULL);
	CHECK(string_equal(manny_path_string(rooted, manny_build_root(rooted)), manny_path_string(build, expected_root)));
	manny_build_destroy(rooted);

	CHECK_OK(manny_execution_create(graph, &execution));
	CHECK(manny_path_resolve(build, root, LIT("discovered after prepare"), &absolute));
	manny_execution_destroy(execution);
	manny_build_destroy(build);
	return true;
}

static b32 test_arena_and_strings(void)
{
    Arena arena = arena_create(KILOBYTES(4));
    char *start;
    String built;
    String copy;
    void *aligned;
    u64 mark;
    Scratch outer;
    Scratch inner;

    CHECK(arena.data != NULL);
    start = arena_top(&arena);
    CHECK(arena_append_text(&arena, "hello") == start);
	CHECK(*(char *)arena_top(&arena) == 0);
    CHECK(arena_append_str(&arena, LIT(" arena")) != NULL);
	CHECK(*(char *)arena_top(&arena) == 0);
    CHECK(arena_appendf(&arena, " %d", 42) != NULL);
	CHECK(*(char *)arena_top(&arena) == 0);
	CHECK(arena_append_char(&arena, '!') != NULL);
	CHECK(*(char *)arena_top(&arena) == 0);
	built = arena_string_from(&arena, start);
	arena_finalize_string(&arena, built);
	CHECK(string_equal(built, LIT("hello arena 42!")));
    CHECK(built.data[built.size] == 0);

    copy = str_push_copy(&arena, built);
    CHECK(string_equal(copy, built));
    CHECK(copy.data[copy.size] == 0);
    CHECK(string_equal(string_slice(copy, 6, 5), LIT("arena")));
	CHECK(string_is_terminated(LIT("hello")));
	CHECK(!string_is_terminated(string_slice(LIT("hello"), 0, 4)));
	CHECK(string_ends_with_insensitive(LIT("build.ELF"), LIT(".elf")));
	CHECK(!string_ends_with_insensitive(LIT("build.lua"), LIT(".elf")));
	{
		String_Array parts = string_split(&arena, LIT("a;;b;"), ';');
		CHECK(parts.count == 4);
		CHECK(string_equal(parts.items[0], LIT("a")));
		CHECK(parts.items[1].size == 0);
		CHECK(string_equal(parts.items[2], LIT("b")));
		CHECK(parts.items[3].size == 0);
	}
	{
		String_Array lines = string_split_lines(&arena, LIT("one\r\ntwo\n"));
		CHECK(lines.count == 3);
		CHECK(string_equal(lines.items[0], LIT("one")));
		CHECK(string_equal(lines.items[1], LIT("two")));
		CHECK(lines.items[2].size == 0);
	}
	{
		String_Array entries = string_split_block(&arena,
			(String){ .data = "one\0two\0", .size = 8 });
		CHECK(entries.count == 2);
		CHECK(string_equal(entries.items[0], LIT("one")));
		CHECK(string_equal(entries.items[1], LIT("two")));
	}
	{
		String left;
		String right;
		CHECK(string_split_first(LIT("NAME=a=b"), '=', &left, &right));
		CHECK(string_equal(left, LIT("NAME")));
		CHECK(string_equal(right, LIT("a=b")));
		CHECK(!string_split_first(LIT("NAME"), '=', &left, &right));
		CHECK(string_equal(
			string_trim_whitespace(LIT(" \t value \r\n")),
			LIT("value")));
	}

    CHECK(arena_push(&arena, 1) != NULL);
    aligned = arena_push_zero_aligned(&arena, 32, 32);
    CHECK(aligned != NULL);
    CHECK((uintptr_t)aligned % 32 == 0);
    CHECK(((u8 *)aligned)[0] == 0 && ((u8 *)aligned)[31] == 0);

    mark = arena_mark(&arena);
    CHECK(arena_push_zero(&arena, 128) != NULL);
    arena_restore(&arena, mark);
    CHECK(arena_mark(&arena) == mark);
    arena_destroy(&arena);

    outer = begin_scratch();
    CHECK(arena_append_text(outer.arena, "outer") != NULL);
    inner = begin_scratch();
	CHECK(inner.arena == outer.arena);
    CHECK(arena_append_text(inner.arena, "inner") != NULL);
    end_scratch(inner);
    CHECK(arena_mark(outer.arena) == inner.restore_used);
	{
		Scratch separate = begin_different_scratch(outer.arena);
		CHECK(separate.arena != outer.arena);
		CHECK(arena_append_text(separate.arena, "separate") != NULL);
		end_scratch(separate);
	}
    end_scratch(outer);
	CHECK(arena_mark(outer.arena) == outer.restore_used);
    destroy_global_scratch();
    return true;
}

typedef struct Scratch_Thread_Test {
    Arena *arena;
    HANDLE ready;
    HANDLE release;
} Scratch_Thread_Test;

static DWORD WINAPI scratch_thread_test_main(void *parameter)
{
    Scratch_Thread_Test *test = parameter;
    Scratch scratch = begin_scratch();
    arena_append_text(scratch.arena, "thread scratch");
    test->arena = scratch.arena;
    SetEvent(test->ready);
    WaitForSingleObject(test->release, INFINITE);
    end_scratch(scratch);
    destroy_global_scratch();
    return 0;
}

static b32 test_thread_local_scratch(void)
{
    Scratch main_scratch = begin_scratch();
    Scratch_Thread_Test tests[2] = {0};
    HANDLE threads[2] = {0};
    HANDLE ready[2] = {0};
    HANDLE release = CreateEventA(NULL, TRUE, FALSE, NULL);
    b32 passed = false;

    if (!release) goto cleanup;
    for (u32 i = 0; i < ARRAY_COUNT(tests); ++i)
    {
        tests[i].ready = CreateEventA(NULL, TRUE, FALSE, NULL);
        tests[i].release = release;
        ready[i] = tests[i].ready;
        if (!ready[i]) goto cleanup;
        threads[i] = CreateThread(NULL, 0, scratch_thread_test_main, tests + i, 0, NULL);
        if (!threads[i]) goto cleanup;
    }
    if (WaitForMultipleObjects(ARRAY_COUNT(ready), ready, TRUE, 5000) != WAIT_OBJECT_0) goto cleanup;
    passed = tests[0].arena && tests[1].arena &&
        tests[0].arena != tests[1].arena &&
        tests[0].arena != main_scratch.arena &&
        tests[1].arena != main_scratch.arena;

cleanup:
    if (release) SetEvent(release);
    for (u32 i = 0; i < ARRAY_COUNT(threads); ++i) {
        if (threads[i]) {
            WaitForSingleObject(threads[i], INFINITE);
            CloseHandle(threads[i]);
        }
        if (ready[i]) CloseHandle(ready[i]);
    }
    if (release) CloseHandle(release);
    end_scratch(main_scratch);
    destroy_global_scratch();
    return passed;
}


int main(void)
{
	static const Manny_Test tests[] = {
		MANNY_TEST(test_arena_and_strings),
		MANNY_TEST(test_thread_local_scratch),
		MANNY_TEST(test_vcvars_cache_application),
		MANNY_TEST(test_high_resolution_timer),
		MANNY_TEST(test_blake3),
		MANNY_TEST(test_build_paths),
	};
	return test_run_suite("base", tests, ARRAY_COUNT(tests));
}
