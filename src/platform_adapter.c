#include "platform_adapter.h"

#define MANNY_PLATFORM_ERROR_OUT_OF_MEMORY UINT32_MAX

b32 manny_platform_file_info(String path, Manny_Platform_File_Info *info)
{
	dy_File_Info shared;
	if (!info) return false;
	if (dy_get_file_info(path, &shared).error) return false;
	info->size = shared.size;
	info->modified_unix_ms = shared.modified_unix_ms;
	info->is_directory = shared.is_directory;
	return true;
}

b32 manny_platform_executable_path(Arena *arena, String *result)
{
	if (!arena || !result) return false;
	return dy_get_executable_path(arena, result).error == DY_ERROR_NONE;
}

b32 manny_platform_current_directory(Arena *arena, String *result)
{
	if (!arena || !result) return false;
	return dy_get_current_directory(arena, result).error == DY_ERROR_NONE;
}

b32 manny_platform_absolute_path(Arena *arena, String path, String *result)
{
	if (!arena || !result) return false;
	return dy_get_absolute_path(arena, path, result).error == DY_ERROR_NONE;
}

b32 manny_platform_read_entire_file(Arena *arena, String path, String *result)
{
	dy_File file;
	u64 size;
	u64 read;
	if (!arena || !result) return false;
	u64 mark = arena_mark(arena);
	if (dy_access_file(path, DY_FILE_OPEN_EXISTING,
		DY_FILE_READ | DY_FILE_SHARE_READ | DY_FILE_SHARE_WRITE | DY_FILE_SHARE_DELETE, &file).error) return false;
	if (dy_get_file_size(file, &size).error || size == UINT64_MAX) goto failure;
	char *data = arena_push(arena, size + 1);
	if (!data) goto failure;
	if (dy_read_file(file, data, size, &read).error || read != size) goto failure;
	dy_close_file(file);
	data[size] = 0;
	result->data = data;
	result->size = size;
	return true;

failure:
	dy_close_file(file);
	arena_restore(arena, mark);
	return false;
}

b32 manny_platform_write_entire_file(String path, const void *data, size_t size)
{
	dy_File file;
	u64 written;
	dy_Result write;
	dy_Result closed;
	if (!data && size) return false;
	if (dy_access_file(path, DY_FILE_CREATE_ALWAYS, DY_FILE_WRITE, &file).error) return false;
	write = dy_write_file(file, data, size, &written);
	closed = dy_close_file(file);
	return !write.error && written == size && !closed.error;
}

b32 manny_platform_create_directory(String path)
{
	return dy_create_directory(path).error == DY_ERROR_NONE;
}

b32 manny_platform_executable_resolves(String name)
{
	return dy_executable_resolves(name);
}

b32 manny_platform_get_environment(String name, Arena *arena, String *value)
{
	dy_Result result;
	if (!arena || !value) return false;
	result = dy_get_env_field(arena, name, value);
	return result.error == DY_ERROR_NONE || result.error == DY_ERROR_NOT_FOUND;
}

b32 manny_platform_set_environment(String name, String value)
{
	dy_Result result = value.data ? dy_set_env_field(name, value) : dy_remove_env_field(name);
	return result.error == DY_ERROR_NONE;
}

b32 manny_platform_local_app_data(Arena *arena, String *result)
{
	return manny_platform_get_environment(LIT("LOCALAPPDATA"), arena, result) && result->size > 0;
}

static b32 append_process_pipe(dy_Process *process, Arena *arena, b32 standard_error, u32 *error_code)
{
	char buffer[4096];
	u64 size;
	b32 end_of_stream;
	dy_Result read = dy_read_process(process, standard_error ? DY_PROCESS_ERROR : DY_PROCESS_OUTPUT,
		buffer, sizeof(buffer), &size, &end_of_stream);
	(void)end_of_stream;
	if (read.error) {
		*error_code = read.os_error ? read.os_error : (u32)read.error;
		return false;
	}
	if (size && !arena_push_copy(arena, size, buffer)) {
		*error_code = MANNY_PLATFORM_ERROR_OUT_OF_MEMORY;
		return false;
	}
	return size != 0;
}

b32 manny_platform_run_command(String command_line, Arena *arena, Manny_Platform_Process_Options options, Manny_Platform_Process_Result *result)
{
	u64 mark;
	dy_Process process;
	dy_Result status;
	b32 completed;
	u32 exit_code;
	if (!arena || !result) return false;
	mark = arena_mark(arena);
	*result = (Manny_Platform_Process_Result){ .exit_code = UINT32_MAX };
	status = dy_start_process(command_line, (dy_Process_Options){
		.working_directory = options.working_directory,
		.capture_output = true,
		.capture_error = options.capture_stderr,
		.hide_window = options.hide_window,
	}, &process);
	if (status.error) {
		result->error_code = status.os_error ? status.os_error : (u32)status.error;
		return false;
	}
	result->launched = true;
	for (;;) {
		while (append_process_pipe(&process, arena, false, &result->error_code)) {}
		if (options.capture_stderr) while (append_process_pipe(&process, arena, true, &result->error_code)) {}
		if (result->error_code) goto failure;
		status = dy_wait_process(process, 1, &completed, &exit_code);
		if (status.error) {
			result->error_code = status.os_error ? status.os_error : (u32)status.error;
			goto failure;
		}
		if (completed) break;
	}
	while (append_process_pipe(&process, arena, false, &result->error_code)) {}
	if (options.capture_stderr) while (append_process_pipe(&process, arena, true, &result->error_code)) {}
	if (result->error_code) goto failure;
	result->exit_code = exit_code;
	result->output.data = (char *)arena->data + mark;
	result->output.size = arena->used - mark;
	dy_close_process(&process);
	return true;

failure:
	dy_close_process(&process);
	arena_restore(arena, mark);
	result->output = (String){0};
	return false;
}

b32 manny_platform_error_message(u32 error_code, Arena *arena, String *result)
{
	if (!error_code || !arena || !result) return false;
	return dy_error_message(arena, error_code, result).error == DY_ERROR_NONE;
}

static dy_Mutex manny_output_mutex;
static b32 manny_output_mutex_initialized;

void manny_platform_enable_console_colors(void)
{
	if (!manny_output_mutex_initialized) {
		dy_init_mutex(&manny_output_mutex);
		manny_output_mutex_initialized = true;
	}
	dy_enable_console_colors(DY_STANDARD_OUTPUT);
	dy_enable_console_colors(DY_STANDARD_ERROR);
}

b32 manny_platform_console_supports_colors(b32 error_stream)
{
	return dy_console_supports_colors(error_stream ? DY_STANDARD_ERROR : DY_STANDARD_OUTPUT);
}

void manny_platform_output_lock(void)
{
	dy_lock_mutex(&manny_output_mutex);
}

void manny_platform_output_unlock(void)
{
	dy_unlock_mutex(&manny_output_mutex);
}
