#include "test.h"

static Manny_Path test_path(Manny_Build *build, String source)
{
	Manny_Path result = {0};
	if (build) manny_path_resolve(build, manny_build_root(build), source, &result);
	return result;
}

static Manny_Fingerprint test_fingerprint(String value)
{
	Manny_Fingerprint result;
	blake3_hasher hasher;
	blake3_hasher_init(&hasher);
	blake3_hasher_update(&hasher, value.data, (size_t)value.size);
	blake3_hasher_finalize(&hasher, result.bytes, sizeof(result.bytes));
	return result;
}

static b32 fingerprints_equal(Manny_Fingerprint left, Manny_Fingerprint right)
{
	return memcmp(left.bytes, right.bytes, MANNY_FINGERPRINT_SIZE) == 0;
}

static b32 record_task_contains(Manny_Build *build, const Build_Record_Task *task, String path)
{
	Manny_Path expected = test_path(build, path);
	if (!task || !manny_path_is_valid(expected)) return false;
	for (u32 i = 0; i < task->dependencies.count; ++i) {
		if (task->dependencies.items[i].atom.id == expected.atom.id) return true;
	}
	return false;
}

static b32 append_task(Arena *arena, Manny_Build *build, Build_Record_Stream *stream, String file,
	String output, String_Array dependencies, u64 output_stamp, String fingerprint)
{
	Manny_Path_Array paths = {0};
	if (dependencies.count) {
		paths.items = arena_push_zero_aligned(arena,
			(u64)dependencies.count * sizeof(*paths.items), _Alignof(Manny_Path));
		if (!paths.items) return false;
	}
	for (u32 i = 0; i < dependencies.count; ++i) {
		paths.items[paths.count] = test_path(build, dependencies.items[i]);
		if (!manny_path_is_valid(paths.items[paths.count])) return false;
		++paths.count;
	}
	return build_record_stream_append_set(stream, file, (Build_Record_Task){
		.output = test_path(build, output),
		.output_stamp = output_stamp,
		.fingerprint = test_fingerprint(fingerprint),
		.dependencies = paths,
	});
}

static b32 snapshot_get(Manny_Build *build, const Build_Record_Stream *stream, Arena *arena,
	String output, Build_Record_Task *task, u32 *task_count)
{
	Build_Record_Snapshot snapshot;
	if (!build_record_stream_snapshot(stream, arena, &snapshot)) return false;
	if (task_count) *task_count = snapshot.task_count;
	return build_record_snapshot_get(&snapshot, test_path(build, output), task);
}

static b32 test_record_round_trip(void)
{
	Manny_Build *build = manny_build_create();
	Arena arena = arena_create(KILOBYTES(64));
	Arena loaded_arena = arena_create(KILOBYTES(64));
	Arena snapshot_arena = arena_create(KILOBYTES(16));
	Build_Record_Stream stream = {0};
	Build_Record_Stream loaded = {0};
	String root = LIT("build\\test_record_round_trip");
	String file = LIT("build\\test_record_round_trip\\nested\\state");
	String dependencies[] = { LIT("src/main file.c"), LIT("include/quoted\"name.h") };
	Build_Record_Task task;
	u32 task_count;

	CHECK(build && arena.data && loaded_arena.data && snapshot_arena.data);
	CHECK(build_record_stream_init(&stream, &arena, build));
	CHECK(build_record_stream_init(&loaded, &loaded_arena, build));
	CHECK(platform_remove_tree(root.data));
	CHECK(build_record_stream_compact(&stream, file));
	CHECK(append_task(&arena, build, &stream, file, LIT("build/main.obj"),
		STRING_ARRAY_FROM(dependencies), 101, LIT("main fingerprint")));
	CHECK(append_task(&arena, build, &stream, file, LIT("build/empty.obj"),
		(String_Array){0}, 202, LIT("empty fingerprint")));
	CHECK(build_record_stream_compact(&stream, file));
	CHECK(build_record_stream_load(&loaded, file) == BUILD_RECORD_OK);
	CHECK(snapshot_get(build, &loaded, &snapshot_arena, LIT("build/main.obj"), &task, &task_count));
	CHECK(task_count == 2 && task.output_stamp == 101 && task.dependencies.count == 2);
	CHECK(fingerprints_equal(task.fingerprint, test_fingerprint(LIT("main fingerprint"))));
	CHECK(record_task_contains(build, &task, dependencies[0]));
	CHECK(record_task_contains(build, &task, dependencies[1]));
	CHECK(snapshot_get(build, &loaded, &snapshot_arena, LIT("build/empty.obj"), &task, NULL));
	CHECK(task.output_stamp == 202 && task.dependencies.count == 0);

	CHECK(platform_remove_tree(root.data));
	build_record_stream_destroy(&loaded);
	build_record_stream_destroy(&stream);
	arena_destroy(&snapshot_arena);
	arena_destroy(&loaded_arena);
	arena_destroy(&arena);
	manny_build_destroy(build);
	return true;
}

static b32 test_record_recovery(void)
{
	static const char malformed[] = "not a Manny state";
	Manny_Build *build = manny_build_create();
	Arena arena = arena_create(KILOBYTES(64));
	Arena loaded_arena = arena_create(KILOBYTES(64));
	Arena io_arena = arena_create(KILOBYTES(64));
	Arena snapshot_arena = arena_create(KILOBYTES(16));
	Build_Record_Stream stream = {0};
	Build_Record_Stream loaded = {0};
	String root = LIT("build\\test_record_recovery");
	String file = LIT("build\\test_record_recovery\\state");
	String missing = LIT("build\\test_record_recovery\\missing");
	String bytes;
	Build_Record_Task task;
	u32 task_count;

	CHECK(build && arena.data && loaded_arena.data && io_arena.data && snapshot_arena.data);
	CHECK(build_record_stream_init(&stream, &arena, build));
	CHECK(build_record_stream_init(&loaded, &loaded_arena, build));
	CHECK(platform_remove_tree(root.data));
	CHECK(build_record_stream_load(&loaded, missing) == BUILD_RECORD_MISSING);
	CHECK(build_record_stream_compact(&stream, file));
	CHECK(append_task(&arena, build, &stream, file, LIT("build/first.obj"), (String_Array){0}, 1, LIT("first")));
	CHECK(append_task(&arena, build, &stream, file, LIT("build/second.obj"), (String_Array){0}, 2, LIT("second")));
	CHECK(build_record_stream_compact(&stream, file));

	CHECK(manny_platform_read_entire_file(&io_arena, file, &bytes));
	CHECK(bytes.size > 16);
	CHECK(manny_platform_write_entire_file(file, bytes.data, bytes.size - 1));
	CHECK(build_record_stream_load(&loaded, file) == BUILD_RECORD_RECOVERED);
	CHECK(snapshot_get(build, &loaded, &snapshot_arena, LIT("build/first.obj"), &task, &task_count));
	CHECK(task_count == 1);

	CHECK(build_record_stream_compact(&stream, file));
	arena_reset(&io_arena);
	CHECK(manny_platform_read_entire_file(&io_arena, file, &bytes));
	bytes.data[bytes.size - 1] ^= 0x5a;
	CHECK(manny_platform_write_entire_file(file, bytes.data, bytes.size));
	CHECK(build_record_stream_load(&loaded, file) == BUILD_RECORD_INVALID);

	CHECK(build_record_stream_compact(&stream, file));
	arena_reset(&io_arena);
	CHECK(manny_platform_read_entire_file(&io_arena, file, &bytes));
	CHECK(bytes.size > 12);
	bytes.data[8] ^= 0x01;
	CHECK(manny_platform_write_entire_file(file, bytes.data, bytes.size));
	CHECK(build_record_stream_load(&loaded, file) == BUILD_RECORD_INVALID);

	CHECK(manny_platform_write_entire_file(file, malformed, sizeof(malformed) - 1));
	CHECK(build_record_stream_load(&loaded, file) == BUILD_RECORD_INVALID);
	CHECK(platform_remove_tree(root.data));
	build_record_stream_destroy(&loaded);
	build_record_stream_destroy(&stream);
	arena_destroy(&snapshot_arena);
	arena_destroy(&io_arena);
	arena_destroy(&loaded_arena);
	arena_destroy(&arena);
	manny_build_destroy(build);
	return true;
}

static b32 test_record_append_and_remove(void)
{
	Manny_Build *build = manny_build_create();
	Arena arena = arena_create(KILOBYTES(64));
	Arena loaded_arena = arena_create(KILOBYTES(64));
	Arena snapshot_arena = arena_create(KILOBYTES(16));
	Build_Record_Stream stream = {0};
	Build_Record_Stream loaded = {0};
	String root = LIT("build\\test_record_append");
	String file = LIT("build\\test_record_append\\state");
	String first[] = { LIT("src/main.c"), LIT("include/common.h") };
	String replacement[] = { LIT("src/main.c"), LIT("include/next.h") };
	Build_Record_Task task;
	u32 task_count;

	CHECK(build && arena.data && loaded_arena.data && snapshot_arena.data);
	CHECK(build_record_stream_init(&stream, &arena, build));
	CHECK(build_record_stream_init(&loaded, &loaded_arena, build));
	CHECK(platform_remove_tree(root.data));
	CHECK(build_record_stream_compact(&stream, file));
	CHECK(append_task(&arena, build, &stream, file, LIT("build/main.obj"),
		STRING_ARRAY_FROM(first), 101, LIT("first")));
	CHECK(append_task(&arena, build, &stream, file, LIT("build/main.obj"),
		STRING_ARRAY_FROM(replacement), 202, LIT("replacement")));
	CHECK(build_record_stream_load(&loaded, file) == BUILD_RECORD_OK);
	CHECK(snapshot_get(build, &loaded, &snapshot_arena, LIT("build/main.obj"), &task, &task_count));
	CHECK(task_count == 1 && task.output_stamp == 202 && task.dependencies.count == 2);
	CHECK(record_task_contains(build, &task, replacement[1]));
	CHECK(!record_task_contains(build, &task, first[1]));

	CHECK(build_record_stream_append_remove(&stream, file, test_path(build, LIT("build/main.obj"))));
	CHECK(build_record_stream_load(&loaded, file) == BUILD_RECORD_OK);
	CHECK(!snapshot_get(build, &loaded, &snapshot_arena, LIT("build/main.obj"), &task, &task_count));
	CHECK(task_count == 0);

	CHECK(platform_remove_tree(root.data));
	build_record_stream_destroy(&loaded);
	build_record_stream_destroy(&stream);
	arena_destroy(&snapshot_arena);
	arena_destroy(&loaded_arena);
	arena_destroy(&arena);
	manny_build_destroy(build);
	return true;
}

static b32 test_record_snapshot_is_immutable(void)
{
	Manny_Build *build = manny_build_create();
	Arena arena = arena_create(KILOBYTES(64));
	Arena snapshot_arena = arena_create(KILOBYTES(16));
	Build_Record_Stream stream = {0};
	Build_Record_Snapshot snapshot;
	String root = LIT("build\\test_record_snapshot");
	String file = LIT("build\\test_record_snapshot\\state");
	String first[] = { LIT("src/main.c") };
	String replacement[] = { LIT("include/next.h") };
	Build_Record_Task task;

	CHECK(build && arena.data && snapshot_arena.data);
	CHECK(build_record_stream_init(&stream, &arena, build));
	CHECK(platform_remove_tree(root.data));
	CHECK(build_record_stream_compact(&stream, file));
	CHECK(append_task(&arena, build, &stream, file, LIT("build/main.obj"),
		STRING_ARRAY_FROM(first), 101, LIT("first")));
	CHECK(build_record_stream_snapshot(&stream, &snapshot_arena, &snapshot));
	CHECK(append_task(&arena, build, &stream, file, LIT("build/main.obj"),
		STRING_ARRAY_FROM(replacement), 202, LIT("replacement")));

	CHECK(build_record_snapshot_get(&snapshot, test_path(build, LIT("build/main.obj")), &task));
	CHECK(task.output_stamp == 101 && record_task_contains(build, &task, first[0]));
	CHECK(!record_task_contains(build, &task, replacement[0]));
	CHECK(snapshot_get(build, &stream, &snapshot_arena, LIT("build/main.obj"), &task, NULL));
	CHECK(task.output_stamp == 202 && record_task_contains(build, &task, replacement[0]));

	CHECK(platform_remove_tree(root.data));
	build_record_stream_destroy(&stream);
	arena_destroy(&snapshot_arena);
	arena_destroy(&arena);
	manny_build_destroy(build);
	return true;
}

int main(void)
{
	static const Manny_Test tests[] = {
		MANNY_TEST(test_record_round_trip),
		MANNY_TEST(test_record_recovery),
		MANNY_TEST(test_record_append_and_remove),
		MANNY_TEST(test_record_snapshot_is_immutable),
	};
	return test_run_suite("record", tests, ARRAY_COUNT(tests));
}
