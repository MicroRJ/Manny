#include "test.h"

static b32 test_manny_script(void)
{
    Arena arena = arena_create(MEGABYTES(16));
    Script *script = script_load(&arena, LIT("build.elf"));
    CHECK(script_is_loaded(script));
    CHECK(script_has_function(script, LIT("build")));
    CHECK(script_has_function(script, LIT("test")));
    CHECK(script_has_function(script, LIT("bless")));
    script_destroy(script);
    arena_destroy(&arena);
    return true;
}

// NOTE(RJ) we must be able to pet manny
static b32 test_pet_manny(void)
{
	Arena arena = arena_create(MEGABYTES(16));
	Script *script = script_load(&arena, LIT("build.elf"));
	CHECK(script_is_loaded(script));
	CHECK(script_has_function(script, LIT("pet")));
	CHECK(script_invoke(script, LIT("pet")));
	script_destroy(script);
	arena_destroy(&arena);
	return true;
}

static b32 test_script_functions(void)
{
    static const char source[] =
        "build := fun() {\n"
        "    ret manny.build({\n"
        "        targets = {},\n"
        "        options = {workers = 1, verbosity = 0},\n"
        "    })\n"
        "}\n"
        "clean := fun() { ret 1 }\n"
        "ret {build = build, clean = clean}\n";
    String path = LIT("build/test_script_functions.elf");
    Arena arena = arena_create(MEGABYTES(16));
    CHECK(!dy_create_directories(LIT("build")).error);
    CHECK(manny_platform_write_entire_file(path, source, sizeof(source) - 1));
    Script *script = script_load(&arena, path);
    CHECK(!dy_remove_file(path).error);
    CHECK(script_is_loaded(script));
    String_Array functions = script_functions(script);
    CHECK(functions.count == 2);
    CHECK(script_has_function(script, LIT("build")));
    CHECK(script_has_function(script, LIT("clean")));
    CHECK(!script_has_function(script, LIT("missing")));
    CHECK(script_invoke(script, LIT("build")));
    CHECK(script_invoke(script, LIT("clean")));
    CHECK(!script_invoke(script, LIT("missing")));
    script_destroy(script);
    arena_destroy(&arena);
    return true;
}


int main(void)
{
	static const Manny_Test tests[] = {
		MANNY_TEST(test_manny_script),
		MANNY_TEST(test_pet_manny),
		MANNY_TEST(test_script_functions),
	};
	return test_run_suite("script", tests, ARRAY_COUNT(tests));
}
