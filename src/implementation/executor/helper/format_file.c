/**
 * @file format_file.c
 * @brief Checked file access and metadata operations for the README format.
 */

#include "implementation/executor/helper/format_file.h"

#include <limits.h>
#include <stdio.h>

#include "implementation/executor/helper/binary_encoding.h"
#include "implementation/executor/helper/create_header.h"
#include "implementation/executor/helper/create_metatable.h"
#include "implementation/executor/helper/create_table_entries.h"
#include "implementation/executor/helper/validate_metadata_layout.h"

static bool _read_entry(const struct executor_format_file* file,
                        const struct executor_format_section* section,
                        size_t index, size_t entry_size, size_t count,
                        uint8_t* bytes) {
        uint64_t offset;

        if (file == NULL || section == NULL || bytes == NULL ||
            index >= count ||
            !executor_helper_multiply_u64(index, entry_size, &offset) ||
            !executor_helper_add_u64(section->offset, offset, &offset) ||
            !executor_format_file_read(file, offset, bytes, entry_size)) {
                return false;
        }
        return true;
}

bool executor_format_file_load(struct database* database,
                               struct executor_format_file* file) {
        long size;
        uint8_t header_bytes[EXECUTOR_FORMAT_HEADER_SIZE];
        uint8_t metatable_bytes[EXECUTOR_FORMAT_METATABLE_SIZE];

        if (database == NULL || database->fp == NULL || file == NULL ||
            fseek(database->fp, 0, SEEK_END) != 0 ||
            (size = ftell(database->fp)) < 0 ||
            fseek(database->fp, 0, SEEK_SET) != 0 ||
            fread(header_bytes, 1, sizeof header_bytes, database->fp) !=
                sizeof header_bytes ||
            fread(metatable_bytes, 1, sizeof metatable_bytes, database->fp) !=
                sizeof metatable_bytes ||
            !executor_helper_read_header(header_bytes, sizeof header_bytes,
                                         &file->header) ||
            !executor_helper_read_metatable(
                metatable_bytes, sizeof metatable_bytes, &file->metatable) ||
            !executor_helper_validate_metadata_layout(
                (size_t)size, &file->header, &file->metatable)) {
                return false;
        }
        file->database = database;
        file->size     = (size_t)size;
        return true;
}

bool executor_format_file_read(const struct executor_format_file* file,
                               uint64_t offset, void* output, size_t length) {
        if (file == NULL || file->database == NULL ||
            file->database->fp == NULL || (length != 0 && output == NULL) ||
            !executor_helper_range_valid(file->size, offset, length) ||
            offset > LONG_MAX ||
            fseek(file->database->fp, (long)offset, SEEK_SET) != 0) {
                return false;
        }
        return length == 0 ||
               fread(output, 1, length, file->database->fp) == length;
}

bool executor_format_file_append(struct executor_format_file* file,
                                 const void* bytes, size_t length,
                                 uint64_t* offset) {
        long end;

        if (file == NULL || file->database == NULL ||
            file->database->fp == NULL || file->database->read_only ||
            offset == NULL || (length != 0 && bytes == NULL) ||
            length > SIZE_MAX - file->size ||
            fseek(file->database->fp, 0, SEEK_END) != 0 ||
            (end = ftell(file->database->fp)) < 0 || end > LONG_MAX ||
            fwrite(bytes, 1, length, file->database->fp) != length) {
                return false;
        }
        *offset = (uint64_t)end;
        file->size += length;
        database_mark_modified(file->database);
        return true;
}

bool executor_format_file_commit(struct executor_format_file* file) {
        uint8_t header_bytes[EXECUTOR_FORMAT_HEADER_SIZE];
        uint8_t metatable_bytes[EXECUTOR_FORMAT_METATABLE_SIZE];

        if (file == NULL || file->database == NULL ||
            file->database->fp == NULL || file->database->read_only ||
            !executor_helper_create_header(header_bytes, sizeof header_bytes,
                                           &file->header) ||
            !executor_helper_create_metatable(
                metatable_bytes, sizeof metatable_bytes, &file->metatable) ||
            fseek(file->database->fp, 0, SEEK_SET) != 0 ||
            fwrite(header_bytes, 1, sizeof header_bytes, file->database->fp) !=
                sizeof header_bytes ||
            fwrite(metatable_bytes, 1, sizeof metatable_bytes,
                   file->database->fp) != sizeof metatable_bytes ||
            fflush(file->database->fp) != 0) {
                return false;
        }
        database_mark_modified(file->database);
        return true;
}

bool executor_format_file_read_interface_entry(
    const struct executor_format_file* file, size_t index,
    struct executor_format_interface_entry* entry) {
        uint8_t bytes[EXECUTOR_FORMAT_INTERFACE_ENTRY_SIZE];
        return file != NULL && entry != NULL &&
               _read_entry(file, &file->metatable.interfaces, index,
                           sizeof bytes, file->header.interface_count, bytes) &&
               executor_helper_read_interface_entry(bytes, sizeof bytes, entry);
}

bool executor_format_file_read_class_entry(
    const struct executor_format_file* file, size_t index,
    struct executor_format_class_entry* entry) {
        uint8_t bytes[EXECUTOR_FORMAT_CLASS_ENTRY_SIZE];
        return file != NULL && entry != NULL &&
               _read_entry(file, &file->metatable.classes, index, sizeof bytes,
                           file->header.class_count, bytes) &&
               executor_helper_read_class_entry(bytes, sizeof bytes, entry);
}

bool executor_format_file_read_function_entry(
    const struct executor_format_file* file, size_t index,
    struct executor_format_function_entry* entry) {
        uint8_t bytes[EXECUTOR_FORMAT_FUNCTION_ENTRY_SIZE];
        return file != NULL && entry != NULL &&
               _read_entry(file, &file->metatable.functions, index,
                           sizeof bytes, file->header.function_count, bytes) &&
               executor_helper_read_function_entry(bytes, sizeof bytes, entry);
}
