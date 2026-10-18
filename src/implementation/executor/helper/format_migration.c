#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

/**
 * @file format_migration.c
 * @brief Migrate legacy object storage to compact object logs.
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
            bytes_written == NULL || record_length < 2) {
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

struct migration_object_record {
        uint8_t* bytes;
        uint32_t length;
};

struct migration_id_set {
        uint64_t* slots;
        size_t capacity;
        size_t count;
};

static size_t _migration_id_hash(uint64_t object_id, size_t capacity) {
        object_id ^= object_id >> 33;
        object_id *= UINT64_C(0xff51afd7ed558ccd);
        object_id ^= object_id >> 33;
        object_id *= UINT64_C(0xc4ceb9fe1a85ec53);
        object_id ^= object_id >> 33;
        return (size_t)object_id & (capacity - 1);
}

static bool _migration_id_set_add(struct migration_id_set* set,
                                  uint64_t object_id, bool* already_seen) {
        size_t slot;

        if (set == NULL || already_seen == NULL || object_id == 0) {
                return false;
        }
        if (set->capacity == 0 || set->count + 1 > set->capacity / 2) {
                size_t new_capacity =
                    set->capacity == 0 ? 16 : set->capacity * 2;
                uint64_t* grown;
                size_t i;

                if (new_capacity < set->capacity ||
                    new_capacity > SIZE_MAX / sizeof *set->slots) {
                        return false;
                }
                grown = calloc(new_capacity, sizeof *grown);
                if (grown == NULL) {
                        return false;
                }
                for (i = 0; i < set->capacity; i++) {
                        if (set->slots[i] != 0) {
                                size_t target = _migration_id_hash(
                                    set->slots[i], new_capacity);
                                while (grown[target] != 0) {
                                        target =
                                            (target + 1) & (new_capacity - 1);
                                }
                                grown[target] = set->slots[i];
                        }
                }
                free(set->slots);
                set->slots    = grown;
                set->capacity = new_capacity;
        }
        slot = _migration_id_hash(object_id, set->capacity);
        while (set->slots[slot] != 0) {
                if (set->slots[slot] == object_id) {
                        *already_seen = true;
                        return true;
                }
                slot = (slot + 1) & (set->capacity - 1);
        }
        set->slots[slot] = object_id;
        set->count++;
        *already_seen = false;
        return true;
}

static bool _compact_v1_object_record(const uint8_t* record,
                                      uint32_t record_length,
                                      uint8_t** compact_record,
                                      uint32_t* compact_length) {
        struct executor_format_object_header header;
        struct executor_format_object_field* fields = NULL;
        uint8_t* output                             = NULL;
        size_t offset;
        size_t output_length;
        size_t i;
        bool result = false;

        if (record == NULL || compact_record == NULL ||
            compact_length == NULL ||
            record_length < EXECUTOR_FORMAT_OBJECT_HEADER_SIZE ||
            !executor_helper_read_object_header(record, record_length,
                                                &header) ||
            header.record_length != record_length || header.object_id == 0 ||
            (header.flags & ~EXECUTOR_FORMAT_OBJECT_FLAG_DELETED) != 0 ||
            ((header.flags & EXECUTOR_FORMAT_OBJECT_FLAG_DELETED) != 0 &&
             header.field_count != 0)) {
                return false;
        }
        if (header.field_count != 0) {
                fields = calloc(header.field_count, sizeof *fields);
                if (fields == NULL) {
                        return false;
                }
        }
        offset = EXECUTOR_FORMAT_OBJECT_HEADER_SIZE;
        for (i = 0; i < header.field_count; i++) {
                struct executor_format_object_field_view field;

                if (!executor_helper_read_object_field(record, record_length,
                                                       &offset, &field)) {
                        goto done;
                }
                fields[i].field_id     = field.field_id;
                fields[i].type_code    = field.type_code;
                fields[i].value        = field.value;
                fields[i].value_length = field.value_length;
        }
        if (offset != record_length ||
            !executor_helper_compact_object_record_size(
                header.object_id, header.flags, fields, header.field_count,
                &output_length) ||
            output_length > UINT32_MAX) {
                goto done;
        }
        output = malloc(output_length);
        if (output == NULL ||
            !executor_helper_create_compact_object_record(
                output, output_length, header.object_id, header.flags, fields,
                header.field_count, &output_length)) {
                goto done;
        }
        *compact_record = output;
        *compact_length = (uint32_t)output_length;
        output          = NULL;
        result          = true;
done:
        free(output);
        free(fields);
        return result;
}

static bool _migrate_contiguous_class_data(
    struct executor_format_file* source, FILE* output,
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
                uint8_t* record                = NULL;
                uint8_t* compact_record        = NULL;
                uint32_t compact_record_length = 0;
                uint64_t next_cursor;
                uint64_t new_head;
                uint64_t node_length;

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
                    !executor_helper_add_u64(source_cursor, record_length,
                                             &next_cursor)) {
                        free(record);
                        return false;
                }
                if (!_compact_v1_object_record(record, record_length,
                                               &compact_record,
                                               &compact_record_length) ||
                    !executor_helper_read_object_header(record, record_length,
                                                        &parsed) ||
                    parsed.flags != 0 ||
                    !_write_object_log(output, compact_record,
                                       compact_record_length, log_head,
                                       &new_head, &node_length) ||
                    !executor_helper_add_u64(log_length, node_length,
                                             &log_length)) {
                        free(compact_record);
                        free(record);
                        return false;
                }
                free(compact_record);
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

static bool _migrate_log_class_data(struct executor_format_file* source,
                                    FILE* output,
                                    struct executor_format_class_entry* entry) {
        struct migration_id_set seen_ids        = {0};
        struct migration_object_record* records = NULL;
        size_t record_count                     = 0;
        size_t record_capacity                  = 0;
        uint64_t node_offset                    = entry->data_offset;
        uint64_t visited_length                 = 0;
        uint64_t live_count                     = 0;
        uint64_t new_head                       = 0;
        uint64_t new_length                     = 0;
        size_t i;
        bool result = false;

        if (entry->data_length > SIZE_MAX ||
            (entry->data_offset == 0 && entry->data_length != 0)) {
                goto done;
        }
        while (node_offset != 0) {
                uint8_t
                    node_header[EXECUTOR_FORMAT_OBJECT_LOG_NODE_HEADER_SIZE];
                uint64_t previous_offset;
                uint64_t record_offset;
                uint64_t node_length;
                uint32_t record_length;
                uint8_t* record         = NULL;
                uint8_t* compact_record = NULL;
                uint32_t compact_length = 0;
                struct executor_format_object_header header;
                bool already_seen;

                if (!executor_helper_range_valid(
                        source->size, node_offset,
                        EXECUTOR_FORMAT_OBJECT_LOG_NODE_HEADER_SIZE) ||
                    !executor_format_file_read(source, node_offset, node_header,
                                               sizeof node_header)) {
                        goto done;
                }
                previous_offset = executor_helper_read_u64(node_header);
                record_length   = executor_helper_read_u32(node_header + 8);
                if (record_length < EXECUTOR_FORMAT_OBJECT_HEADER_SIZE ||
                    (previous_offset != 0 && previous_offset >= node_offset) ||
                    !executor_helper_add_u64(
                        node_offset,
                        EXECUTOR_FORMAT_OBJECT_LOG_NODE_HEADER_SIZE,
                        &record_offset) ||
                    !executor_helper_range_valid(source->size, record_offset,
                                                 record_length) ||
                    !executor_helper_add_u64(
                        record_length,
                        EXECUTOR_FORMAT_OBJECT_LOG_NODE_HEADER_SIZE,
                        &node_length) ||
                    !executor_helper_add_u64(visited_length, node_length,
                                             &visited_length) ||
                    visited_length > entry->data_length) {
                        goto done;
                }
                record = malloc(record_length);
                if (record == NULL ||
                    !executor_format_file_read(source, record_offset, record,
                                               record_length) ||
                    !_compact_v1_object_record(record, record_length,
                                               &compact_record,
                                               &compact_length) ||
                    !executor_helper_read_object_header(record, record_length,
                                                        &header)) {
                        free(compact_record);
                        free(record);
                        goto done;
                }
                if (!_migration_id_set_add(&seen_ids, header.object_id,
                                           &already_seen)) {
                        free(compact_record);
                        free(record);
                        goto done;
                }
                if (!already_seen &&
                    (header.flags & EXECUTOR_FORMAT_OBJECT_FLAG_DELETED) == 0) {
                        struct migration_object_record* grown;

                        if (record_count == record_capacity) {
                                size_t new_capacity = record_capacity == 0
                                                          ? 8
                                                          : record_capacity * 2;
                                if (new_capacity < record_capacity ||
                                    new_capacity > SIZE_MAX / sizeof *records) {
                                        free(compact_record);
                                        free(record);
                                        goto done;
                                }
                                grown = realloc(records,
                                                new_capacity * sizeof *records);
                                if (grown == NULL) {
                                        free(compact_record);
                                        free(record);
                                        goto done;
                                }
                                records         = grown;
                                record_capacity = new_capacity;
                        }
                        records[record_count].bytes  = compact_record;
                        records[record_count].length = compact_length;
                        record_count++;
                        compact_record = NULL;
                        if (live_count == UINT64_MAX) {
                                free(record);
                                goto done;
                        }
                        live_count++;
                }
                free(compact_record);
                free(record);
                node_offset = previous_offset;
        }
        if (visited_length != entry->data_length ||
            live_count != entry->object_count) {
                goto done;
        }
        for (i = record_count; i > 0; i--) {
                uint64_t record_node_offset;
                uint64_t node_length;

                if (!_write_object_log(output, records[i - 1].bytes,
                                       records[i - 1].length, new_head,
                                       &record_node_offset, &node_length) ||
                    !executor_helper_add_u64(new_length, node_length,
                                             &new_length)) {
                        goto done;
                }
                new_head = record_node_offset;
        }
        entry->data_offset  = new_head;
        entry->data_length  = new_length;
        entry->object_count = live_count;
        result              = true;
done:
        for (i = 0; i < record_count; i++) {
                free(records[i].bytes);
        }
        free(records);
        free(seen_ids.slots);
        return result;
}

static bool _migrate_to_compact(struct executor_format_file* source,
                                FILE* output) {
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

        if ((header.version != EXECUTOR_FORMAT_VERSION_CONTIGUOUS &&
             header.version != EXECUTOR_FORMAT_VERSION_OBJECT_LOG) ||
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

        header.version = EXECUTOR_FORMAT_VERSION_COMPACT_OBJECT_LOG;
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
                if (!(source->header.version ==
                              EXECUTOR_FORMAT_VERSION_CONTIGUOUS
                          ? _migrate_contiguous_class_data(source, output,
                                                           &entry)
                          : _migrate_log_class_data(source, output, &entry)) ||
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
        if (source.header.version == EXECUTOR_FORMAT_VERSION_CURRENT) {
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
        migration_ok = _migrate_to_compact(&source, output);
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
