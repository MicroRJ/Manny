#ifndef BUILD_RECORD_INTERNAL_H
#define BUILD_RECORD_INTERNAL_H

#include "build_record_stream.h"

b32 build_record_reserve_tasks(Build_Record_Stream *stream, u32 needed);
u32 build_record_task_index(const Build_Record_Stream *stream, Bob_Path output);
b32 build_record_set(Build_Record_Stream *stream, Build_Record_Task task);
b32 build_record_remove(Build_Record_Stream *stream, Bob_Path output);
void build_record_replace_tasks(Build_Record_Stream *stream, const Build_Record_Stream *replacement);

#endif
