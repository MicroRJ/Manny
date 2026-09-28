#include "platform_adapter.h"
#include "platform.h"

#define MANNY_PLATFORM_ERROR_OUT_OF_MEMORY UINT32_MAX

b32 manny_platform_file_info(String path, Manny_Platform_File_Info *info)
{
	day_File_Info_Result shared;
	if (!info) return false;
	shared = day_get_file_info(path);
	if (shared.error) return false;
	info->size = shared.info.size;
	info->modified_unix_ms = shared.info.modified_unix_ms;
	info->is_directory = shared.info.is_directory;
	return true;
}

b32 manny_platform_executable_path(Arena *arena, String *result)
{
	if (!arena || !result) return false;
	Platform_String_Result query = platform_get_executable_path(NULL, 0);
	if (query.error || query.required_capacity == 0) return false;
	u64 mark = arena_mark(arena);
	char *data = arena_reserve(arena, query.required_capacity);
	if (!data) return false;
	Platform_String_Result filled = platform_get_executable_path(data, query.required_capacity);
	if (filled.error || !arena_push(arena, query.required_capacity)) {
		arena_restore(arena, mark);
		return false;
	}
	result->data = data;
	result->size = filled.size;
	return true;
}

b32 manny_platform_current_directory(Arena *arena, String *result)
{
	if (!arena || !result) return false;
	Platform_String_Result query = platform_get_current_directory(NULL, 0);
	if (query.error || query.required_capacity == 0) return false;
	u64 mark = arena_mark(arena);
	char *data = arena_reserve(arena, query.required_capacity);
	if (!data) return false;
	Platform_String_Result filled = platform_get_current_directory(data, query.required_capacity);
	if (filled.error || !arena_push(arena, query.required_capacity)) {
		arena_restore(arena, mark);
		return false;
	}
	result->data = data;
	result->size = filled.size;
	return true;
}

b32 manny_platform_absolute_path(Arena *arena, String path, String *result)
{
	if (!arena || !result || !string_is_terminated(path)) return false;
	Platform_String_Result query = platform_get_absolute_path(path.data, NULL, 0);
	if (query.error || query.required_capacity == 0) return false;
	u64 mark = arena_mark(arena);
	char *data = arena_reserve(arena, query.required_capacity);
	if (!data) return false;
	Platform_String_Result filled = platform_get_absolute_path(path.data, data, query.required_capacity);
	if (filled.error || !arena_push(arena, query.required_capacity)) {
		arena_restore(arena, mark);
		return false;
	}
	result->data = data;
	result->size = filled.size;
	return true;
}

b32 manny_platform_read_entire_file(Arena *arena, String path, String *result)
{
	day_File_Result opened;
	day_File_Size_Result size;
	day_IO_Result read;
	if (!arena || !result) return false;
	u64 mark = arena_mark(arena);
	opened = day_access_file(path, DAY_FILE_OPEN_EXISTING,
		DAY_FILE_READ | DAY_FILE_SHARE_READ | DAY_FILE_SHARE_WRITE | DAY_FILE_SHARE_DELETE);
	if (opened.error) return false;
	size = day_get_file_size(opened.file);
	if (size.error || size.size == UINT64_MAX) goto failure;
	char *data = arena_push(arena, size.size + 1);
	if (!data) goto failure;
	read = day_read_file(opened.file, data, size.size);
	if (read.error || read.size != size.size) goto failure;
	day_close_file(opened.file);
	data[size.size] = 0;
	result->data = data;
	result->size = size.size;
	return true;

failure:
	day_close_file(opened.file);
	arena_restore(arena, mark);
	return false;
}

b32 manny_platform_write_entire_file(String path, const void *data, size_t size)
{
	day_File_Result opened;
	day_IO_Result written;
	day_Result closed;
	if (!data && size) return false;
	opened = day_access_file(path, DAY_FILE_CREATE_ALWAYS, DAY_FILE_WRITE);
	if (opened.error) return false;
	written = day_write_file(opened.file, data, size);
	closed = day_close_file(opened.file);
	return !written.error && written.size == size && !closed.error;
}

b32 manny_platform_create_directory(String path)
{
	return string_is_terminated(path) && platform_create_directory(path.data);
}

b32 manny_platform_executable_resolves(String name)
{
	return string_is_terminated(name) && platform_executable_resolves(name.data);
}

b32 manny_platform_get_environment(String name, Arena *arena, String *value)
{
	if (!string_is_terminated(name) || !arena || !value) return false;
	*value = (String){0};
	Platform_Environment_Result query = platform_get_environment(name.data, NULL, 0);
	if (query.error) return false;
	if (!query.found) return true;
	u64 mark = arena_mark(arena);
	char *data = arena_reserve(arena, query.required_capacity);
	if (!data) return false;
	Platform_Environment_Result read = platform_get_environment(name.data, data, query.required_capacity);
	if (read.error || !read.found || !arena_push(arena, query.required_capacity)) {
		arena_restore(arena, mark);
		return false;
	}
	value->data = data;
	value->size = read.size;
	return true;
}

b32 manny_platform_get_environment_block(Arena *arena, String *block)
{
	if (!arena || !block) return false;
	*block = (String){0};
	Platform_String_Result query = platform_get_environment_block(NULL, 0);
	if (query.error || query.required_capacity == 0) return false;
	u64 mark = arena_mark(arena);
	char *data = arena_reserve(arena, query.required_capacity);
	if (!data) return false;
	Platform_String_Result read = platform_get_environment_block(data, query.required_capacity);
	if (read.error || !arena_push(arena, query.required_capacity)) {
		arena_restore(arena, mark);
		return false;
	}
	block->data = data;
	block->size = read.size;
	return true;
}

b32 manny_platform_set_environment(String name, String value)
{
	if (!string_is_terminated(name) || (value.data && !string_is_terminated(value))) return false;
	return platform_set_environment(name.data, value.data).error == 0;
}

b32 manny_platform_local_app_data(Arena *arena, String *result)
{
	return manny_platform_get_environment(LIT("LOCALAPPDATA"), arena, result) && result->size > 0;
}

static b32 append_process_pipe(Platform_Process *process, Arena *arena, b32 standard_error, u32 *error_code)
{
	char buffer[4096];
	Platform_Process_Read_Result read = standard_error ? platform_read_process_error(process, buffer, sizeof(buffer)) : platform_read_process_output(process, buffer, sizeof(buffer));
	if (read.error) {
		*error_code = read.os_error ? read.os_error : (u32)read.error;
		return false;
	}
	if (read.size && !arena_push_copy(arena, read.size, buffer)) {
		*error_code = MANNY_PLATFORM_ERROR_OUT_OF_MEMORY;
		return false;
	}
	return read.size != 0;
}

b32 manny_platform_run_command(String command_line, Arena *arena, Manny_Platform_Process_Options options, Manny_Platform_Process_Result *result)
{
	u64 mark;
	Platform_Process_Start_Result start;
	Platform_Process_Wait_Result wait = {0};
	if (!string_is_terminated(command_line) ||
		(options.working_directory.data &&
			!string_is_terminated(options.working_directory)) || !arena || !result) return false;
	mark = arena_mark(arena);
	*result = (Manny_Platform_Process_Result){ .exit_code = UINT32_MAX };
	start = platform_start_process(command_line.data, (Platform_Process_Options){
		.working_directory = options.working_directory.data,
		.capture_standard_output = true,
		.capture_standard_error = options.capture_stderr,
		.hide_window = options.hide_window,
	});
	if (start.error) {
		result->error_code = start.os_error ? start.os_error : (u32)start.error;
		return false;
	}
	result->launched = true;
	for (;;) {
		while (append_process_pipe(&start.process, arena, false, &result->error_code)) {}
		if (options.capture_stderr) while (append_process_pipe(&start.process, arena, true, &result->error_code)) {}
		if (result->error_code) goto failure;
		wait = platform_wait_process(start.process, 1);
		if (wait.status == PLATFORM_PROCESS_WAIT_COMPLETED) break;
		if (wait.status == PLATFORM_PROCESS_WAIT_FAILED) {
			result->error_code = wait.os_error ? wait.os_error : (u32)wait.error;
			goto failure;
		}
	}
	while (append_process_pipe(&start.process, arena, false, &result->error_code)) {}
	if (options.capture_stderr) while (append_process_pipe(&start.process, arena, true, &result->error_code)) {}
	if (result->error_code) goto failure;
	result->exit_code = wait.exit_code;
	result->output.data = (char *)arena->data + mark;
	result->output.size = arena->used - mark;
	platform_close_process(&start.process);
	return true;

failure:
	platform_close_process(&start.process);
	arena_restore(arena, mark);
	result->output = (String){0};
	return false;
}

b32 manny_platform_error_message(u32 error_code, Arena *arena, String *result)
{
	if (!error_code || !arena || !result) return false;
	Platform_String_Result query = platform_error_message(error_code, NULL, 0);
	if (query.error || query.required_capacity == 0) return false;
	u64 mark = arena_mark(arena);
	char *data = arena_reserve(arena, query.required_capacity);
	if (!data) return false;
	Platform_String_Result read = platform_error_message(error_code, data, query.required_capacity);
	if (read.error || !arena_push(arena, query.required_capacity)) {
		arena_restore(arena, mark);
		return false;
	}
	result->data = data;
	result->size = read.size;
	return true;
}

static Platform_Mutex manny_output_mutex;
static b32 manny_output_mutex_initialized;

void manny_platform_enable_console_colors(void)
{
	if (!manny_output_mutex_initialized) {
		platform_init_mutex(&manny_output_mutex);
		manny_output_mutex_initialized = true;
	}
	platform_enable_console_colors(PLATFORM_STANDARD_OUTPUT);
	platform_enable_console_colors(PLATFORM_STANDARD_ERROR);
}

b32 manny_platform_console_supports_colors(b32 error_stream)
{
	return platform_console_supports_colors(error_stream ? PLATFORM_STANDARD_ERROR : PLATFORM_STANDARD_OUTPUT);
}

void manny_platform_output_lock(void)
{
	platform_lock_mutex(&manny_output_mutex);
}

void manny_platform_output_unlock(void)
{
	platform_unlock_mutex(&manny_output_mutex);
}
