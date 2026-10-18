#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

/**
 * @file format_migration.c
 * @brief Upgrade contiguous object segments to append-only object logs.
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "implementation/executor/helper/binary_encoding.h"
#include "implementation/executor/helper/create_header.h"
#include "implementation/executor/helper/create_metatable.h"
#include "implementation/executor/helper/create_object_record.h"
#include "implementation/executor/helper/create_table_entries.h"
#include "implementation/executor/helper/format_file.h"
#include "implementation/executor/helper/validate_metadata_layout.h"

static bool _append_bytes(FILE* output, const void* bytes, size_t length,
                          uint64_t* offset) {
        long end;

        if (output == NULL || (length != 0 && bytes == NULL) ||
            offset == NULL || fseek(output, 0, SEEK_END) != 0 ||
            (end = ftell(output)) < 0 ||
            (uintmax_t)end > (uintmax_t)UINT64_MAX ||
            fwrite(bytes, 1, length, output) != length) {
                return false;
        }
        *offset = (uint64_t)end;
        return true;
}

static bool _write_at(FILE* output, uint64_t offset, const void* bytes,
                      size_t length) {
        return output != NULL && offset <= LONG_MAX &&
               fseek(output, (long)offset, SEEK_SET) == 0 &&
               fwrite(bytes, 1, length, output) == length;
}

static bool _write_object_log(FILE* output, const uint8_t* record,
                              uint32_t record_length, uint64_t previous,
                              uint64_t* new_head, uint64_t* bytes_written) {
        uint8_t* node;
        size_t node_length;
        uint64_t node_offset;
        bool ok;

        if (output == NULL || record == NULL || new_head == NULL ||
            bytes_written == NULL ||
            record_length < EXECUTOR_FORMAT_OBJECT_HEADER_SIZE) {
                return false;
        }
        node_length = (size_t)record_length;
        if (node_length >
            SIZE_MAX - EXECUTOR_FORMAT_OBJECT_LOG_NODE_HEADER_SIZE) {
                return false;
        }
        node_length += EXECUTOR_FORMAT_OBJECT_LOG_NODE_HEADER_SIZE;
        node = malloc(node_length);
        if (node == NULL) {
                return false;
        }
        executor_helper_write_u64(node, previous);
        executor_helper_write_u32(node + 8, record_length);
        memcpy(node + EXECUTOR_FORMAT_OBJECT_LOG_NODE_HEADER_SIZE, record,
               record_length);
        ok = _append_bytes(output, node, node_length, &node_offset);
        free(node);
        if (!ok) {
                return false;
        }
        *new_head      = node_offset;
        *bytes_written = node_length;
        return true;
}

static bool _migrate_class_data(struct executor_format_file* source,
                                FILE* output,
                                struct executor_format_class_entry* entry) {
        uint64_t source_cursor = 0;
        uint64_t log_head      = 0;
        uint64_t log_length    = 0;
        uint64_t record_count  = 0;

        if (entry->data_length > SIZE_MAX ||
            !executor_helper_range_valid(source->size, entry->data_offset,
                                         entry->data_length)) {
                return false;
        }
        while (source_cursor < entry->data_length) {
                uint8_t record_header[EXECUTOR_FORMAT_OBJECT_HEADER_SIZE];
                struct executor_format_object_header parsed;
                uint32_t record_length;
                uint8_t* record;
                uint64_t next_cursor;
                uint64_t new_head;
                uint64_t node_length;
                size_t field_cursor;
                size_t field_index;

                if (entry->data_length - source_cursor <
                        EXECUTOR_FORMAT_OBJECT_HEADER_SIZE ||
                    !executor_format_file_read(
                        source, entry->data_offset + source_cursor,
                        record_header, sizeof record_header)) {
                        return false;
                }
                record_length = executor_helper_read_u32(record_header);
                if (record_length < EXECUTOR_FORMAT_OBJECT_HEADER_SIZE ||
                    record_length > entry->data_length - source_cursor) {
                        return false;
                }
                record = malloc(record_length);
                if (record == NULL ||
                    !executor_format_file_read(
                        source, entry->data_offset + source_cursor, record,
                        record_length) ||
                    !executor_helper_read_object_header(record, record_length,
                                                        &parsed) ||
                    parsed.record_length != record_length ||
                    parsed.object_id == 0 || parsed.flags != 0 ||
                    !executor_helper_add_u64(source_cursor, record_length,
                                             &next_cursor) ||
                    !_write_object_log(output, record, record_length, log_head,
                                       &new_head, &node_length) ||
                    !executor_helper_add_u64(log_length, node_length,
                                             &log_length)) {
                        free(record);
                        return false;
                }
                field_cursor = EXECUTOR_FORMAT_OBJECT_HEADER_SIZE;
                for (field_index = 0; field_index < parsed.field_count;
                     field_index++) {
                        struct executor_format_object_field_view field;

                        if (!executor_helper_read_object_field(
                                record, parsed.record_length, &field_cursor,
                                &field)) {
                                free(record);
                                return false;
                        }
                }
                if (field_cursor != parsed.record_length) {
                        free(record);
                        return false;
                }
                free(record);
                log_head      = new_head;
                source_cursor = next_cursor;
                if (record_count == UINT64_MAX) {
                        return false;
                }
                record_count++;
        }
        if (record_count != entry->object_count ||
            (record_count == 0 && entry->data_length != 0)) {
                return false;
        }
        entry->data_offset = log_head;
        entry->data_length = log_length;
        return true;
}

static bool _migrate_to_log(struct executor_format_file* source, FILE* output) {
        struct executor_format_header header       = source->header;
        struct executor_format_metatable metatable = {0};
        size_t interface_table_length;
        size_t class_table_length;
        size_t function_table_length;
        uint64_t interface_table_length_u64;
        uint64_t class_table_length_u64;
        uint64_t function_table_length_u64;
        uint8_t* interface_table = NULL;
        uint8_t* class_table     = NULL;
        uint8_t* function_table  = NULL;
        uint64_t interface_table_offset;
        uint64_t class_table_offset;
        uint64_t function_table_offset;
        uint64_t cursor;
        uint8_t header_bytes[EXECUTOR_FORMAT_HEADER_SIZE];
        uint8_t metatable_bytes[EXECUTOR_FORMAT_METATABLE_SIZE];
        long output_size;
        size_t i;
        bool result = false;

        if (header.version != EXECUTOR_FORMAT_VERSION_CONTIGUOUS ||
            !executor_helper_multiply_u64(header.interface_count,
                                          EXECUTOR_FORMAT_INTERFACE_ENTRY_SIZE,
                                          &interface_table_length_u64) ||
            !executor_helper_multiply_u64(header.class_count,
                                          EXECUTOR_FORMAT_CLASS_ENTRY_SIZE,
                                          &class_table_length_u64) ||
            !executor_helper_multiply_u64(header.function_count,
                                          EXECUTOR_FORMAT_FUNCTION_ENTRY_SIZE,
                                          &function_table_length_u64) ||
            interface_table_length_u64 > (uint64_t)SIZE_MAX ||
            class_table_length_u64 > (uint64_t)SIZE_MAX ||
            function_table_length_u64 > (uint64_t)SIZE_MAX) {
                return false;
        }
        interface_table_length = (size_t)interface_table_length_u64;
        class_table_length     = (size_t)class_table_length_u64;
        function_table_length  = (size_t)function_table_length_u64;
        interface_table =
            calloc(interface_table_length == 0 ? 1 : interface_table_length, 1);
        class_table =
            calloc(class_table_length == 0 ? 1 : class_table_length, 1);
        function_table =
            calloc(function_table_length == 0 ? 1 : function_table_length, 1);
        if (interface_table == NULL || class_table == NULL ||
            function_table == NULL) {
                goto done;
        }

        cursor = EXECUTOR_FORMAT_HEADER_SIZE + EXECUTOR_FORMAT_METATABLE_SIZE;
        interface_table_offset = interface_table_length == 0 ? 0 : cursor;
        cursor += interface_table_length;
        class_table_offset = class_table_length == 0 ? 0 : cursor;
        cursor += class_table_length;
        function_table_offset = function_table_length == 0 ? 0 : cursor;
        cursor += function_table_length;
        metatable.interfaces.offset = interface_table_offset;
        metatable.interfaces.length = interface_table_length;
        metatable.classes.offset    = class_table_offset;
        metatable.classes.length    = class_table_length;
        metatable.functions.offset  = function_table_offset;
        metatable.functions.length  = function_table_length;

        header.version = EXECUTOR_FORMAT_VERSION_OBJECT_LOG;
        if (!executor_helper_create_header(header_bytes, sizeof header_bytes,
                                           &header) ||
            !executor_helper_create_metatable(
                metatable_bytes, sizeof metatable_bytes, &metatable) ||
            fwrite(header_bytes, 1, sizeof header_bytes, output) !=
                sizeof header_bytes ||
            fwrite(metatable_bytes, 1, sizeof metatable_bytes, output) !=
                sizeof metatable_bytes ||
            (interface_table_length != 0 &&
             fwrite(interface_table, 1, interface_table_length, output) !=
                 interface_table_length) ||
            (class_table_length != 0 &&
             fwrite(class_table, 1, class_table_length, output) !=
                 class_table_length) ||
            (function_table_length != 0 &&
             fwrite(function_table, 1, function_table_length, output) !=
                 function_table_length)) {
                goto done;
        }

        for (i = 0; i < header.interface_count; i++) {
                struct executor_format_interface_entry entry;
                uint64_t definition_offset;
                uint8_t* source_entry =
                    malloc(EXECUTOR_FORMAT_INTERFACE_ENTRY_SIZE);
                uint8_t* definition = NULL;
                size_t definition_length;

                if (source_entry == NULL ||
                    !executor_format_file_read(
                        source,
                        source->metatable.interfaces.offset +
                            i * EXECUTOR_FORMAT_INTERFACE_ENTRY_SIZE,
                        source_entry, EXECUTOR_FORMAT_INTERFACE_ENTRY_SIZE) ||
                    !executor_helper_read_interface_entry(
                        source_entry, EXECUTOR_FORMAT_INTERFACE_ENTRY_SIZE,
                        &entry)) {
                        free(source_entry);
                        goto done;
                }
                free(source_entry);
                definition_length = entry.definition_length;
                if (definition_length == 0 ||
                    !executor_helper_range_valid(source->size,
                                                 entry.definition_offset,
                                                 definition_length)) {
                        goto done;
                }
                definition = malloc(definition_length);
                if (definition == NULL ||
                    !executor_format_file_read(source, entry.definition_offset,
                                               definition, definition_length) ||
                    !_append_bytes(output, definition, definition_length,
                                   &definition_offset)) {
                        free(definition);
                        goto done;
                }
                free(definition);
                entry.definition_offset = definition_offset;
                if (!executor_helper_create_interface_entry(
                        interface_table +
                            i * EXECUTOR_FORMAT_INTERFACE_ENTRY_SIZE,
                        EXECUTOR_FORMAT_INTERFACE_ENTRY_SIZE, &entry)) {
                        goto done;
                }
        }

        for (i = 0; i < header.class_count; i++) {
                struct executor_format_class_entry entry;
                uint64_t definition_offset;
                uint8_t source_entry[EXECUTOR_FORMAT_CLASS_ENTRY_SIZE];
                uint8_t* definition = NULL;
                size_t definition_length;

                if (!executor_format_file_read(
                        source,
                        source->metatable.classes.offset +
                            i * EXECUTOR_FORMAT_CLASS_ENTRY_SIZE,
                        source_entry, sizeof source_entry) ||
                    !executor_helper_read_class_entry(
                        source_entry, sizeof source_entry, &entry)) {
                        goto done;
                }
                definition_length = entry.definition_length;
                if (definition_length == 0 ||
                    !executor_helper_range_valid(source->size,
                                                 entry.definition_offset,
                                                 definition_length)) {
                        goto done;
                }
                definition = malloc(definition_length);
                if (definition == NULL ||
                    !executor_format_file_read(source, entry.definition_offset,
                                               definition, definition_length) ||
                    !_append_bytes(output, definition, definition_length,
                                   &definition_offset)) {
                        free(definition);
                        goto done;
                }
                free(definition);
                entry.definition_offset = definition_offset;
                if (!_migrate_class_data(source, output, &entry) ||
                    !executor_helper_create_class_entry(
                        class_table + i * EXECUTOR_FORMAT_CLASS_ENTRY_SIZE,
                        EXECUTOR_FORMAT_CLASS_ENTRY_SIZE, &entry)) {
                        goto done;
                }
        }

        for (i = 0; i < header.function_count; i++) {
                struct executor_format_function_entry entry;
                uint8_t source_entry[EXECUTOR_FORMAT_FUNCTION_ENTRY_SIZE];
                uint8_t* definition = NULL;
                size_t definition_length;
                uint64_t definition_offset;

                if (!executor_format_file_read(
                        source,
                        source->metatable.functions.offset +
                            i * EXECUTOR_FORMAT_FUNCTION_ENTRY_SIZE,
                        source_entry, sizeof source_entry) ||
                    !executor_helper_read_function_entry(
                        source_entry, sizeof source_entry, &entry)) {
                        goto done;
                }
                definition_length = entry.definition_length;
                if (definition_length == 0 ||
                    !executor_helper_range_valid(source->size,
                                                 entry.definition_offset,
                                                 definition_length)) {
                        goto done;
                }
                definition = malloc(definition_length);
                if (definition == NULL ||
                    !executor_format_file_read(source, entry.definition_offset,
                                               definition, definition_length) ||
                    !_append_bytes(output, definition, definition_length,
                                   &definition_offset)) {
                        free(definition);
                        goto done;
                }
                free(definition);
                entry.definition_offset = definition_offset;
                if (!executor_helper_create_function_entry(
                        function_table +
                            i * EXECUTOR_FORMAT_FUNCTION_ENTRY_SIZE,
                        EXECUTOR_FORMAT_FUNCTION_ENTRY_SIZE, &entry)) {
                        goto done;
                }
        }

        if ((interface_table_length != 0 &&
             !_write_at(output, interface_table_offset, interface_table,
                        interface_table_length)) ||
            (class_table_length != 0 &&
             !_write_at(output, class_table_offset, class_table,
                        class_table_length)) ||
            (function_table_length != 0 &&
             !_write_at(output, function_table_offset, function_table,
                        function_table_length)) ||
            fseek(output, 0, SEEK_END) != 0 ||
            (output_size = ftell(output)) < 0 ||
            (uintmax_t)output_size > SIZE_MAX ||
            !executor_helper_validate_metadata_layout((size_t)output_size,
                                                      &header, &metatable) ||
            !executor_helper_create_header(header_bytes, sizeof header_bytes,
                                           &header) ||
            !executor_helper_create_metatable(
                metatable_bytes, sizeof metatable_bytes, &metatable) ||
            !_write_at(output, 0, header_bytes, sizeof header_bytes) ||
            !_write_at(output, EXECUTOR_FORMAT_HEADER_SIZE, metatable_bytes,
                       sizeof metatable_bytes)) {
                goto done;
        }
        result = true;
done:
        free(interface_table);
        free(class_table);
        free(function_table);
        return result;
}

bool executor_format_file_migrate(struct database* database) {
        struct executor_format_file source;
        char* resolved_path  = NULL;
        char* temporary_path = NULL;
        size_t path_length;
        int descriptor = -1;
        FILE* output   = NULL;
        struct stat original_stat;
        bool migration_ok;
        bool result = false;

        if (database == NULL || database->path == NULL ||
            database->fp == NULL || database->read_only ||
            !database->format_mode ||
            !executor_format_file_load(database, &source)) {
                return false;
        }
        if (source.header.version == EXECUTOR_FORMAT_VERSION_OBJECT_LOG) {
                return true;
        }
        resolved_path = realpath(database->path, NULL);
        if (resolved_path == NULL) {
                return false;
        }
        path_length = strlen(resolved_path);
        if (path_length > SIZE_MAX - sizeof ".migrate.XXXXXX") {
                free(resolved_path);
                return false;
        }
        temporary_path = malloc(path_length + sizeof ".migrate.XXXXXX");
        if (temporary_path == NULL) {
                free(resolved_path);
                return false;
        }
        snprintf(temporary_path, path_length + sizeof ".migrate.XXXXXX",
                 "%s.migrate.XXXXXX", resolved_path);
        descriptor = mkstemp(temporary_path);
        if (descriptor < 0) {
                goto done;
        }
        if (fstat(fileno(database->fp), &original_stat) != 0 ||
            fchmod(descriptor, original_stat.st_mode & 07777) != 0) {
                goto done;
        }
        output = fdopen(descriptor, "w+b");
        if (output == NULL) {
                goto done;
        }
        descriptor   = -1;
        migration_ok = _migrate_to_log(&source, output);
        if (migration_ok) {
                migration_ok =
                    fflush(output) == 0 && fsync(fileno(output)) == 0;
        }
        if (!migration_ok) {
                goto done;
        }
        if (fclose(database->fp) != 0) {
                database->fp = fopen(database->path, "r+b");
                goto done;
        }
        database->fp = NULL;
        if (rename(temporary_path, resolved_path) != 0) {
                database->fp = fopen(database->path, "r+b");
                goto done;
        }
        database->fp       = output;
        output             = NULL;
        database->modified = false;
        result             = true;
done:
        if (output != NULL) {
                fclose(output);
        }
        if (descriptor >= 0) {
                close(descriptor);
        }
        if (!result) {
                unlink(temporary_path);
        }
        free(temporary_path);
        free(resolved_path);
        return result;
}
