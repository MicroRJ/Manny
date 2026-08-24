#include "test.h"

static Bob_Path test_path(Bob_Build *build, String source)
{
	Bob_Path result = {0};
	if (build) bob_path_resolve(build, bob_build_root(build), source, &result);
	return result;
}

static Bob_Fingerprint test_fingerprint(String value)
{
	Bob_Fingerprint result;
	blake3_hasher hasher;
	blake3_hasher_init(&hasher);
	blake3_hasher_update(&hasher, value.data, (size_t)value.size);
	blake3_hasher_finalize(&hasher, result.bytes, sizeof(result.bytes));
	return result;
}

static b32 test_record_stress(void)
{
	enum { TASK_COUNT = 1886, DEPENDENCY_COUNT = 300 };
	Bob_Build *bob = bob_build_create();
	Arena source_arena = arena_create(MEGABYTES(1));
	Arena state_arena = arena_create(MEGABYTES(16));
	Arena loaded_arena = arena_create(MEGABYTES(16));
	Arena snapshot_arena = arena_create(KILOBYTES(256));
	Build_Record_Stream state_stream = {0};
	Build_Record_Stream loaded_stream = {0};
	String_Array dependencies = {0};
	Bob_Path_Array dependency_paths = {0};
	String root = LIT("build\\build_state_stress");
	String path = LIT("build\\build_state_stress\\state");
	Bob_Platform_File_Info info;
	u64 frequency = platform_counter_frequency();
	u64 construction_started;
	u64 construction_finished;
	u64 save_finished;
	u64 load_finished;
	b32 result = false;

	arena_set_name(&source_arena, "build state stress source");
	arena_set_name(&state_arena, "build state stress state");
	arena_set_name(&loaded_arena, "build state stress loaded state");

#define CHECK_STRESS(condition)                                                 \
	do {                                                                         \
		if (!(condition)) {                                                        \
			printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);          \
			goto cleanup;                                                           \
		}                                                                          \
	} while (0)

	CHECK_STRESS(bob && source_arena.data && state_arena.data && loaded_arena.data && snapshot_arena.data);
	CHECK_STRESS(build_record_stream_init(&state_stream, &state_arena, bob) && build_record_stream_init(&loaded_stream, &loaded_arena, bob));
	CHECK_STRESS(platform_remove_tree(root.data));
	CHECK_STRESS(build_record_stream_compact(&state_stream, path));
	dependencies.items = arena_push_zero_aligned(&source_arena,
		DEPENDENCY_COUNT * sizeof(*dependencies.items), _Alignof(String));
	dependency_paths.items = arena_push_zero_aligned(&source_arena, DEPENDENCY_COUNT * sizeof(*dependency_paths.items), _Alignof(Bob_Path));
	CHECK_STRESS(dependencies.items != NULL && dependency_paths.items != NULL);
	for (u32 i = 0; i < DEPENDENCY_COUNT; ++i) {
		char path[256];
		int length = snprintf(path, sizeof(path),
			"G:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Tools\\MSVC\\14.44.35207\\include\\synthetic\\header_%04u.h",
			i);
		CHECK_STRESS(length > 0 && (size_t)length < sizeof(path));
		dependencies.items[i] = str_push_copy(&source_arena,
			string_from_data(path, (u64)length));
		CHECK_STRESS(dependencies.items[i].data != NULL);
		dependency_paths.items[i] = test_path(bob, dependencies.items[i]);
		CHECK_STRESS(bob_path_is_valid(dependency_paths.items[i]));
		++dependencies.count;
		++dependency_paths.count;
	}

	construction_started = platform_counter();
	for (u32 i = 0; i < TASK_COUNT; ++i) {
		char output[128];
		int length = snprintf(output, sizeof(output),
			"build\\godot\\synthetic_%04u.windows.template_debug.x86_64.o", i);
		CHECK_STRESS(length > 0 && (size_t)length < sizeof(output));
		CHECK_STRESS(build_record_stream_append_set(&state_stream, path, (Build_Record_Task){
			.output = test_path(bob, string_from_data(output, (u64)length)),
			.fingerprint = test_fingerprint(string_from_data(output, (u64)length)),
			.dependencies = dependency_paths,
		}));
	}
	construction_finished = platform_counter();
	CHECK_STRESS(build_record_stream_compact(&state_stream, path));
	save_finished = platform_counter();
	CHECK_STRESS(bob_platform_file_info(path, &info));
	CHECK_STRESS(build_record_stream_load(&loaded_stream, path) ==
		BUILD_RECORD_OK);
	load_finished = platform_counter();
	Build_Record_Snapshot snapshot;
	CHECK_STRESS(build_record_stream_snapshot(&loaded_stream, &snapshot_arena, &snapshot));
	CHECK_STRESS(snapshot.task_count == TASK_COUNT);
	Build_Record_Task task;
	CHECK_STRESS(build_record_snapshot_get(&snapshot, test_path(bob, LIT("build\\godot\\synthetic_0000.windows.template_debug.x86_64.o")), &task));
	CHECK_STRESS(build_record_snapshot_get(&snapshot, test_path(bob, LIT("build\\godot\\synthetic_1885.windows.template_debug.x86_64.o")), &task));

	printf("\n  record-stream stress measurements\n");
	printf("    tasks: %u\n", (u32)TASK_COUNT);
	printf("    dependency references: %u\n",
		(u32)(TASK_COUNT * DEPENDENCY_COUNT));
	printf("    unique dependency paths: %u\n", (u32)DEPENDENCY_COUNT);
	printf("    path reuse: %.1fx\n", (double)TASK_COUNT);
	printf("    in-memory state: %.2f MiB (%.2f s to construct)\n",
		(double)state_arena.used / (double)MEGABYTES(1),
		(double)(construction_finished - construction_started) / (double)frequency);
	printf("    binary state stream: %.2f MiB (%.2f s to save)\n",
		(double)info.size / (double)MEGABYTES(1),
		(double)(save_finished - construction_finished) / (double)frequency);
	printf("    loaded state: %.2f MiB (%.2f s to load)\n",
		(double)loaded_arena.used / (double)MEGABYTES(1),
		(double)(load_finished - save_finished) / (double)frequency);
	result = true;

cleanup:
	platform_remove_tree(root.data);
	build_record_stream_destroy(&loaded_stream);
	build_record_stream_destroy(&state_stream);
	arena_destroy(&snapshot_arena);
	arena_destroy(&loaded_arena);
	arena_destroy(&state_arena);
	arena_destroy(&source_arena);
	bob_build_destroy(bob);
#undef CHECK_STRESS
	return result;
}


int main(void)
{
	static const Bob_Test tests[] = {
		BOB_TEST(test_record_stress),
	};
	return test_run_suite("record stress", tests, ARRAY_COUNT(tests));
}
