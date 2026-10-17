/**
 * @file format_file.h
 * @brief Checked file access and metadata operations for the README format.
 */

#ifndef IMPLEMENTATION_EXECUTOR_HELPER_FORMAT_FILE_H
#define IMPLEMENTATION_EXECUTOR_HELPER_FORMAT_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "implementation/executor/helper/format_types.h"
#include "interface/storage/database.h"

struct executor_format_file {
        struct database* database;
        struct executor_format_header header;
        struct executor_format_metatable metatable;
        size_t size;
};

bool executor_format_file_load(struct database* database,
                               struct executor_format_file* file);
bool executor_format_file_read(const struct executor_format_file* file,
                               uint64_t offset, void* output, size_t length);
bool executor_format_file_append(struct executor_format_file* file,
                                 const void* bytes, size_t length,
                                 uint64_t* offset);
bool executor_format_file_commit(struct executor_format_file* file);
bool executor_format_file_read_interface_entry(
    const struct executor_format_file* file, size_t index,
    struct executor_format_interface_entry* entry);
bool executor_format_file_read_class_entry(
    const struct executor_format_file* file, size_t index,
    struct executor_format_class_entry* entry);
bool executor_format_file_read_function_entry(
    const struct executor_format_file* file, size_t index,
    struct executor_format_function_entry* entry);

#endif
