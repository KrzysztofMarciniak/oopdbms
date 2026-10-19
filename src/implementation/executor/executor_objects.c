/**
 * @file executor_objects.c
 * @brief Executor objects implementation.
 */

#include "implementation/executor/executor_internal.h"

struct object_log_record {
        uint8_t* bytes;
        size_t length;
        uint64_t object_id;
        bool deleted;
};

static bool _serialize_object_fields(
    uint16_t format_version, uint64_t object_id, uint16_t flags,
    const struct executor_format_object_field* fields, size_t count,
    uint8_t** bytes, size_t* length);

static bool _read_object_record_header(
    uint16_t format_version, const uint8_t* record, size_t record_length,
    struct executor_format_object_header* header) {
        if (format_version == EXECUTOR_FORMAT_VERSION_OBJECT_LOG) {
                return record_length >= EXECUTOR_FORMAT_OBJECT_HEADER_SIZE &&
                       executor_helper_read_object_header(record, record_length,
                                                          header) &&
                       header->record_length == record_length &&
                       header->object_id != 0 &&
                       (header->flags & ~EXECUTOR_FORMAT_OBJECT_FLAG_DELETED) ==
                           0 &&
                       ((header->flags & EXECUTOR_FORMAT_OBJECT_FLAG_DELETED) ==
                            0 ||
                        header->field_count == 0);
        }
        if (format_version == EXECUTOR_FORMAT_VERSION_COMPACT_OBJECT_LOG) {
                size_t fields_offset;

                return executor_helper_read_compact_object_header(
                    record, record_length, header, &fields_offset);
        }
        return false;
}

static bool _read_object_log_node(
    const struct executor_format_file* file, uint64_t node_offset,
    uint64_t* previous_offset, uint8_t** record_bytes,
    struct executor_format_object_header* header) {
        uint8_t node_header[EXECUTOR_FORMAT_OBJECT_LOG_NODE_HEADER_SIZE];
        uint64_t record_offset;
        uint32_t record_length;
        uint8_t* record;

        if (file == NULL || node_offset == 0 || previous_offset == NULL ||
            record_bytes == NULL || header == NULL ||
            !executor_helper_range_valid(
                file->size, node_offset,
                EXECUTOR_FORMAT_OBJECT_LOG_NODE_HEADER_SIZE) ||
            !executor_format_file_read(file, node_offset, node_header,
                                       sizeof node_header)) {
                return false;
        }
        *previous_offset = executor_helper_read_u64(node_header);
        record_length    = executor_helper_read_u32(node_header + 8);
        if (record_length == 0 ||
            (*previous_offset != 0 && *previous_offset >= node_offset) ||
            !executor_helper_add_u64(
                node_offset, EXECUTOR_FORMAT_OBJECT_LOG_NODE_HEADER_SIZE,
                &record_offset) ||
            !executor_helper_range_valid(file->size, record_offset,
                                         record_length)) {
                return false;
        }
        record = malloc(record_length);
        if (record == NULL || !executor_format_file_read(
                                  file, record_offset, record, record_length)) {
                free(record);
                return false;
        }
        if (!_read_object_record_header(file->header.version, record,
                                        record_length, header)) {
                free(record);
                return false;
        }
        *record_bytes = record;
        return true;
}

static int _compare_object_log_records(const void* left, const void* right) {
        const struct object_log_record* a = left;
        const struct object_log_record* b = right;

        if (a->object_id < b->object_id) {
                return -1;
        }
        if (a->object_id > b->object_id) {
                return 1;
        }
        return 0;
}

static void _free_object_log_records(struct object_log_record* records,
                                     size_t count) {
        size_t i;

        for (i = 0; i < count; i++) {
                free(records[i].bytes);
        }
        free(records);
}

static size_t _object_id_hash(uint64_t object_id, size_t capacity) {
        object_id ^= object_id >> 33;
        object_id *= UINT64_C(0xff51afd7ed558ccd);
        object_id ^= object_id >> 33;
        object_id *= UINT64_C(0xc4ceb9fe1a85ec53);
        object_id ^= object_id >> 33;
        return (size_t)object_id & (capacity - 1);
}

static bool _remember_object_id(uint64_t** slots, size_t* capacity,
                                size_t* count, uint64_t object_id,
                                bool* already_seen) {
        size_t slot;

        if (slots == NULL || capacity == NULL || count == NULL ||
            already_seen == NULL || object_id == 0) {
                return false;
        }
        if (*capacity == 0 || *count + 1 > *capacity / 2) {
                size_t new_capacity = *capacity == 0 ? 16 : *capacity * 2;
                uint64_t* grown;
                size_t i;

                if (new_capacity < *capacity ||
                    new_capacity > SIZE_MAX / sizeof **slots) {
                        return false;
                }
                grown = calloc(new_capacity, sizeof *grown);
                if (grown == NULL) {
                        return false;
                }
                for (i = 0; i < *capacity; i++) {
                        if ((*slots)[i] != 0) {
                                size_t new_slot =
                                    _object_id_hash((*slots)[i], new_capacity);
                                while (grown[new_slot] != 0) {
                                        new_slot =
                                            (new_slot + 1) & (new_capacity - 1);
                                }
                                grown[new_slot] = (*slots)[i];
                        }
                }
                free(*slots);
                *slots    = grown;
                *capacity = new_capacity;
        }
        slot = _object_id_hash(object_id, *capacity);
        while ((*slots)[slot] != 0) {
                if ((*slots)[slot] == object_id) {
                        *already_seen = true;
                        return true;
                }
                slot = (slot + 1) & (*capacity - 1);
        }
        (*slots)[slot] = object_id;
        (*count)++;
        *already_seen = false;
        return true;
}

static bool _collect_object_log_records(
    const struct executor_format_file* file,
    const struct executor_format_class_entry* entry,
    struct object_log_record** output, size_t* output_count) {
        struct object_log_record* records = NULL;
        size_t count                      = 0;
        size_t capacity                   = 0;
        size_t seen_count                 = 0;
        size_t seen_capacity              = 0;
        uint64_t* seen_ids                = NULL;
        uint64_t node_offset;
        uint64_t total_length = 0;

        if (file == NULL || entry == NULL || output == NULL ||
            output_count == NULL ||
            (file->header.version != EXECUTOR_FORMAT_VERSION_OBJECT_LOG &&
             file->header.version !=
                 EXECUTOR_FORMAT_VERSION_COMPACT_OBJECT_LOG)) {
                return false;
        }
        node_offset = entry->data_offset;
        while (node_offset != 0) {
                uint64_t previous_offset;
                uint8_t* bytes = NULL;
                struct executor_format_object_header header;
                uint64_t node_length;
                bool already_seen = false;

                if (!_read_object_log_node(file, node_offset, &previous_offset,
                                           &bytes, &header) ||
                    !executor_helper_add_u64(
                        header.record_length,
                        EXECUTOR_FORMAT_OBJECT_LOG_NODE_HEADER_SIZE,
                        &node_length) ||
                    !executor_helper_add_u64(total_length, node_length,
                                             &total_length) ||
                    total_length > entry->data_length) {
                        free(bytes);
                        free(seen_ids);
                        _free_object_log_records(records, count);
                        return false;
                }
                if (!_remember_object_id(&seen_ids, &seen_capacity, &seen_count,
                                         header.object_id, &already_seen)) {
                        free(bytes);
                        free(seen_ids);
                        _free_object_log_records(records, count);
                        return false;
                }
                if (!already_seen) {
                        struct object_log_record* grown;

                        if (count == capacity) {
                                size_t new_capacity =
                                    capacity == 0 ? 8 : capacity * 2;
                                if (new_capacity < capacity ||
                                    new_capacity > SIZE_MAX / sizeof *records) {
                                        free(bytes);
                                        free(seen_ids);
                                        _free_object_log_records(records,
                                                                 count);
                                        return false;
                                }
                                grown = realloc(records,
                                                new_capacity * sizeof *records);
                                if (grown == NULL) {
                                        free(bytes);
                                        free(seen_ids);
                                        _free_object_log_records(records,
                                                                 count);
                                        return false;
                                }
                                records  = grown;
                                capacity = new_capacity;
                        }
                        records[count].bytes     = bytes;
                        records[count].length    = header.record_length;
                        records[count].object_id = header.object_id;
                        records[count].deleted =
                            (header.flags &
                             EXECUTOR_FORMAT_OBJECT_FLAG_DELETED) != 0;
                        count++;
                        bytes = NULL;
                }
                free(bytes);
                node_offset = previous_offset;
        }
        if (total_length != entry->data_length ||
            (entry->data_offset == 0 && entry->data_length != 0)) {
                free(seen_ids);
                _free_object_log_records(records, count);
                return false;
        }
        free(seen_ids);
        {
                uint64_t live_count = 0;
                size_t i;

                for (i = 0; i < count; i++) {
                        if (!records[i].deleted) {
                                live_count++;
                        }
                }
                if (live_count != entry->object_count) {
                        _free_object_log_records(records, count);
                        return false;
                }
        }
        if (count > 1) {
                qsort(records, count, sizeof *records,
                      _compare_object_log_records);
        }
        *output       = records;
        *output_count = count;
        return true;
}

static bool _find_object_in_class(
    struct executor_format_file* file,
    const struct executor_format_class_entry* entry, uint64_t object_id,
    uint8_t** segment_bytes, size_t* record_offset,
    struct executor_format_object_header* header) {
        if (file->header.version >= EXECUTOR_FORMAT_VERSION_OBJECT_LOG) {
                uint64_t node_offset  = entry->data_offset;
                uint64_t total_length = 0;
                uint8_t* found_record = NULL;
                struct executor_format_object_header found_header = {0};
                bool found                                        = false;
                bool deleted                                      = false;

                while (node_offset != 0) {
                        uint64_t previous_offset;
                        uint8_t* record = NULL;
                        struct executor_format_object_header current;
                        uint64_t node_length;

                        if (!_read_object_log_node(file, node_offset,
                                                   &previous_offset, &record,
                                                   &current) ||
                            !executor_helper_add_u64(
                                current.record_length,
                                EXECUTOR_FORMAT_OBJECT_LOG_NODE_HEADER_SIZE,
                                &node_length) ||
                            !executor_helper_add_u64(total_length, node_length,
                                                     &total_length) ||
                            total_length > entry->data_length) {
                                free(record);
                                free(found_record);
                                return false;
                        }
                        if (!found && current.object_id == object_id) {
                                found = true;
                                deleted =
                                    (current.flags &
                                     EXECUTOR_FORMAT_OBJECT_FLAG_DELETED) != 0;
                                found_header = current;
                                if (!deleted) {
                                        found_record = record;
                                        record       = NULL;
                                }
                        }
                        free(record);
                        node_offset = previous_offset;
                }
                if (total_length != entry->data_length || !found || deleted) {
                        free(found_record);
                        return false;
                }
                if (segment_bytes != NULL) {
                        *segment_bytes = found_record;
                } else {
                        free(found_record);
                }
                if (record_offset != NULL) {
                        *record_offset = 0;
                }
                if (header != NULL) {
                        *header = found_header;
                }
                return true;
        }

        uint8_t* segment = NULL;
        size_t length;
        size_t offset = 0;

        if (entry->data_length > SIZE_MAX ||
            !executor_helper_range_valid(file->size, entry->data_offset,
                                         entry->data_length)) {
                return false;
        }
        length = (size_t)entry->data_length;
        if (length != 0) {
                segment = malloc(length);
                if (segment == NULL ||
                    !executor_format_file_read(file, entry->data_offset,
                                               segment, length)) {
                        free(segment);
                        return false;
                }
        }
        while (offset < length) {
                struct executor_format_object_header current;
                if (!executor_helper_read_object_header(
                        segment + offset, length - offset, &current) ||
                    current.record_length > length - offset) {
                        free(segment);
                        return false;
                }
                if (current.object_id == object_id) {
                        if (segment_bytes != NULL) {
                                *segment_bytes = segment;
                        } else {
                                free(segment);
                        }
                        if (record_offset != NULL) {
                                *record_offset = offset;
                        }
                        if (header != NULL) {
                                *header = current;
                        }
                        return true;
                }
                offset += current.record_length;
        }
        free(segment);
        return false;
}

static bool _object_exists(struct executor_format_file* file, uint32_t class_id,
                           uint64_t object_id) {
        struct executor_format_class_entry entry;
        bool exists;

        if (!_class_entry_by_id(file, class_id, &entry)) {
                return false;
        }
        exists =
            _find_object_in_class(file, &entry, object_id, NULL, NULL, NULL);
        return exists;
}

bool _encode_input_value(struct executor_format_file* file,
                         const struct executor_format_field* field,
                         const struct analyzer_value* input, uint8_t** output,
                         uint32_t* output_length) {
        uint8_t bytes[12];
        size_t written = 0;
        int64_t integer;
        double floating;
        uint64_t object_id;
        struct executor_format_class_entry target;

        if (field == NULL || input == NULL || output == NULL ||
            output_length == NULL) {
                return false;
        }
        switch (field->type_code) {
                case EXECUTOR_FORMAT_TYPE_INTEGER:
                        if (input->kind != ANALYZER_VALUE_INTEGER ||
                            !_parse_i64(input->text, &integer) ||
                            !executor_helper_create_integer_value(
                                bytes, sizeof bytes, integer, &written)) {
                                return false;
                        }
                        break;
                case EXECUTOR_FORMAT_TYPE_FLOAT:
                        if (input->kind == ANALYZER_VALUE_FLOAT) {
                                if (!_parse_double(input->text, &floating)) {
                                        return false;
                                }
                        } else if (input->kind == ANALYZER_VALUE_INTEGER) {
                                if (!_parse_i64(input->text, &integer)) {
                                        return false;
                                }
                                floating = (double)integer;
                        } else {
                                return false;
                        }
                        if (!executor_helper_create_float_value(
                                bytes, sizeof bytes, floating, &written)) {
                                return false;
                        }
                        break;
                case EXECUTOR_FORMAT_TYPE_TEXT:
                        if (input->kind != ANALYZER_VALUE_TEXT ||
                            input->text == NULL ||
                            strlen(input->text) > UINT32_MAX) {
                                return false;
                        }
                        written = strlen(input->text);
                        *output = malloc(written == 0 ? 1 : written);
                        if (*output == NULL ||
                            !executor_helper_create_text_value(
                                *output, written, (const uint8_t*)input->text,
                                written, &written)) {
                                free(*output);
                                *output = NULL;
                                return false;
                        }
                        *output_length = (uint32_t)written;
                        return true;
                case EXECUTOR_FORMAT_TYPE_BOOL:
                        if (input->kind != ANALYZER_VALUE_BOOL ||
                            input->text == NULL ||
                            (strcmp(input->text, "true") != 0 &&
                             strcmp(input->text, "false") != 0) ||
                            !executor_helper_create_bool_value(
                                bytes, sizeof bytes,
                                strcmp(input->text, "true") == 0, &written)) {
                                return false;
                        }
                        break;
                case EXECUTOR_FORMAT_TYPE_POINTER:
                        if (input->kind != ANALYZER_VALUE_POINTER ||
                            input->class_name == NULL ||
                            !_find_class(file, input->class_name, &target) ||
                            target.class_id != field->target_class_id ||
                            !_parse_u64(input->text, &object_id) ||
                            !_object_exists(file, target.class_id, object_id) ||
                            !executor_helper_create_pointer_value(
                                bytes, sizeof bytes, target.class_id, object_id,
                                &written)) {
                                return false;
                        }
                        break;
                default:
                        return false;
        }
        *output = malloc(written == 0 ? 1 : written);
        if (*output == NULL) {
                return false;
        }
        memcpy(*output, bytes, written);
        *output_length = (uint32_t)written;
        return true;
}

bool _schema_field_index(const struct executor_format_field* fields,
                         size_t count, const char* name, size_t* index) {
        size_t i;

        for (i = 0; i < count; i++) {
                if (_field_name_equal(&fields[i], name)) {
                        *index = i;
                        return true;
                }
        }
        return false;
}

static bool _rewrite_object_segment(struct executor_format_file* file,
                                    size_t class_index,
                                    struct executor_format_class_entry* entry,
                                    const uint8_t* segment,
                                    size_t segment_length) {
        uint8_t* definition  = NULL;
        uint64_t data_offset = 0;
        bool result;

        if (segment_length > UINT64_MAX ||
            !_read_definition(file, entry->definition_offset,
                              entry->definition_length, &definition)) {
                return false;
        }
        if (segment_length != 0 &&
            !executor_format_file_append(file, segment, segment_length,
                                         &data_offset)) {
                free(definition);
                return false;
        }
        entry->data_offset = data_offset;
        entry->data_length = segment_length;
        result = _rewrite_class_entry(file, class_index, entry, definition,
                                      entry->definition_length);
        free(definition);
        return result;
}

static bool _append_object_log_version(
    struct executor_format_file* file, size_t class_index,
    struct executor_format_class_entry* entry, const uint8_t* record,
    size_t record_length) {
        uint8_t* node = NULL;
        uint8_t entry_bytes[EXECUTOR_FORMAT_CLASS_ENTRY_SIZE];
        size_t class_count = file->header.class_count;
        size_t node_length;
        uint64_t node_offset;
        uint64_t entry_offset;
        uint64_t updated_data_length;
        bool result = false;

        if ((file->header.version != EXECUTOR_FORMAT_VERSION_OBJECT_LOG &&
             file->header.version !=
                 EXECUTOR_FORMAT_VERSION_COMPACT_OBJECT_LOG) ||
            class_index >= class_count || record == NULL ||
            (file->header.version == EXECUTOR_FORMAT_VERSION_OBJECT_LOG &&
             record_length < EXECUTOR_FORMAT_OBJECT_HEADER_SIZE) ||
            (file->header.version ==
                 EXECUTOR_FORMAT_VERSION_COMPACT_OBJECT_LOG &&
             record_length < 2) ||
            record_length > UINT32_MAX ||
            record_length >
                SIZE_MAX - EXECUTOR_FORMAT_OBJECT_LOG_NODE_HEADER_SIZE ||
            class_count > SIZE_MAX / EXECUTOR_FORMAT_CLASS_ENTRY_SIZE) {
                return false;
        }
        if (file->metatable.classes.length !=
                class_count * EXECUTOR_FORMAT_CLASS_ENTRY_SIZE ||
            !executor_helper_range_valid(file->size,
                                         file->metatable.classes.offset,
                                         file->metatable.classes.length) ||
            !executor_helper_multiply_u64(
                class_index, EXECUTOR_FORMAT_CLASS_ENTRY_SIZE, &entry_offset) ||
            !executor_helper_add_u64(file->metatable.classes.offset,
                                     entry_offset, &entry_offset) ||
            !executor_helper_range_valid(file->size, entry_offset,
                                         EXECUTOR_FORMAT_CLASS_ENTRY_SIZE) ||
            !executor_helper_add_u64(
                entry->data_length,
                EXECUTOR_FORMAT_OBJECT_LOG_NODE_HEADER_SIZE + record_length,
                &updated_data_length)) {
                return false;
        }
        node_length =
            EXECUTOR_FORMAT_OBJECT_LOG_NODE_HEADER_SIZE + record_length;
        node = malloc(node_length);
        if (node == NULL) {
                goto done;
        }
        executor_helper_write_u64(node, entry->data_offset);
        executor_helper_write_u32(node + 8, (uint32_t)record_length);
        memcpy(node + EXECUTOR_FORMAT_OBJECT_LOG_NODE_HEADER_SIZE, record,
               record_length);
        if (!executor_format_file_append(file, node, node_length,
                                         &node_offset)) {
                goto done;
        }
        entry->data_offset = node_offset;
        entry->data_length = updated_data_length;
        if (!executor_helper_create_class_entry(entry_bytes, sizeof entry_bytes,
                                                entry)) {
                goto done;
        }
        if (!executor_format_file_write(file, entry_offset, entry_bytes,
                                        sizeof entry_bytes)) {
                goto done;
        }
        result = executor_format_file_commit(file);
done:
        free(node);
        return result;
}

static enum EXECUTOR_RESULT _new_object(
    struct database* db, const struct analyzer_statement* statement,
    FILE* output) {
        struct executor_format_file file;
        struct executor_format_class_entry entry;
        struct executor_format_field* schema               = NULL;
        struct executor_format_object_field* record_fields = NULL;
        uint8_t* record_bytes                              = NULL;
        uint8_t* old_segment                               = NULL;
        uint8_t* segment                                   = NULL;
        size_t schema_count                                = 0;
        size_t class_index;
        size_t record_length;
        size_t segment_length;
        size_t old_segment_length;
        size_t i;
        uint64_t object_id;

        enum EXECUTOR_RESULT result = EXECUTOR_ERROR_BAD_STATEMENT;

        if (statement->name == NULL || !db->format_mode ||
            !executor_format_file_load(db, &file) ||
            !_class_index_by_name(&file, statement->name, &class_index,
                                  &entry) ||
            !_collect_schema_fields(&file, entry.class_id, &schema,
                                    &schema_count) ||
            schema_count == 0 || schema_count > UINT16_MAX ||
            statement->assignments.items == NULL ||
            statement->assignments.count != schema_count ||
            entry.next_object_id == 0 || entry.next_object_id == UINT64_MAX) {
                goto done;
        }
        object_id     = entry.next_object_id;
        record_fields = calloc(schema_count, sizeof *record_fields);
        if (record_fields == NULL) {
                result = EXECUTOR_ERROR_STORAGE_FAILED;
                goto done;
        }
        for (i = 0; i < statement->assignments.count; i++) {
                const struct analyzer_assignment* assignment =
                    &statement->assignments.items[i];
                size_t field_index;
                size_t previous;
                uint8_t* value_bytes = NULL;
                uint32_t value_length;

                if (assignment->field == NULL ||
                    !_schema_field_index(schema, schema_count,
                                         assignment->field, &field_index)) {
                        goto done;
                }
                for (previous = 0; previous < i; previous++) {
                        if (strcmp(statement->assignments.items[previous].field,
                                   assignment->field) == 0) {
                                goto done;
                        }
                }
                if (!_encode_input_value(&file, &schema[field_index],
                                         &assignment->value, &value_bytes,
                                         &value_length)) {
                        free(value_bytes);
                        goto done;
                }
                record_fields[field_index].field_id =
                    schema[field_index].field_id;
                record_fields[field_index].type_code =
                    schema[field_index].type_code;
                record_fields[field_index].value        = value_bytes;
                record_fields[field_index].value_length = value_length;
        }
        for (i = 0; i < schema_count; i++) {
                if (record_fields[i].value == NULL) {
                        goto done;
                }
        }
        if (!_serialize_object_fields(file.header.version, object_id, 0,
                                      record_fields, schema_count,
                                      &record_bytes, &record_length)) {
                result = EXECUTOR_ERROR_STORAGE_FAILED;
                goto done;
        }
        if (file.header.version >= EXECUTOR_FORMAT_VERSION_OBJECT_LOG) {
                if (entry.object_count == UINT64_MAX) {
                        goto done;
                }
                entry.object_count++;
                entry.next_object_id++;
                if (!_append_object_log_version(&file, class_index, &entry,
                                                record_bytes, record_length)) {
                        result = EXECUTOR_ERROR_SAVE_FAILED;
                        goto done;
                }
                if (fprintf(output, "%s.%llu\n", statement->name,
                            (unsigned long long)object_id) < 0) {
                        result = EXECUTOR_ERROR_OUTPUT_FAILED;
                        goto done;
                }
                result = EXECUTOR_OK;
                goto done;
        }
        if (entry.data_length > SIZE_MAX ||
            !executor_helper_range_valid(file.size, entry.data_offset,
                                         entry.data_length)) {
                result = EXECUTOR_ERROR_STORAGE_FAILED;
                goto done;
        }
        old_segment_length = (size_t)entry.data_length;
        if (old_segment_length != 0) {
                old_segment = malloc(old_segment_length);
                if (old_segment == NULL ||
                    !executor_format_file_read(&file, entry.data_offset,
                                               old_segment,
                                               old_segment_length)) {
                        result = EXECUTOR_ERROR_STORAGE_FAILED;
                        goto done;
                }
        }
        if (record_length > SIZE_MAX - old_segment_length) {
                result = EXECUTOR_ERROR_STORAGE_FAILED;
                goto done;
        }
        segment_length = old_segment_length + record_length;
        segment        = malloc(segment_length);
        if (segment == NULL) {
                result = EXECUTOR_ERROR_STORAGE_FAILED;
                goto done;
        }
        if (old_segment_length != 0) {
                memcpy(segment, old_segment, old_segment_length);
        }
        memcpy(segment + old_segment_length, record_bytes, record_length);
        if (entry.object_count == UINT64_MAX) {
                goto done;
        }
        entry.object_count++;
        entry.next_object_id++;
        if (!_rewrite_object_segment(&file, class_index, &entry, segment,
                                     segment_length) ||
            !executor_format_file_commit(&file)) {
                result = EXECUTOR_ERROR_SAVE_FAILED;
                goto done;
        }
        if (fprintf(output, "%s.%llu\n", statement->name,
                    (unsigned long long)object_id) < 0) {
                result = EXECUTOR_ERROR_OUTPUT_FAILED;
                goto done;
        }
        result = EXECUTOR_OK;
done:
        if (record_fields != NULL) {
                for (i = 0; i < schema_count; i++) {
                        free((void*)record_fields[i].value);
                }
        }
        free(record_fields);
        free(record_bytes);
        free(old_segment);
        free(segment);
        _free_schema_fields(schema, schema_count);
        return result;
}

void _free_object_fields(struct executor_format_object_field* fields,
                         size_t count) {
        size_t i;
        if (fields == NULL) {
                return;
        }
        for (i = 0; i < count; i++) {
                free((void*)fields[i].value);
        }
        free(fields);
}

static bool _schema_index_by_id(const struct executor_format_field* schema,
                                size_t count, uint32_t field_id,
                                size_t* index) {
        size_t i;
        for (i = 0; i < count; i++) {
                if (schema[i].field_id == field_id) {
                        *index = i;
                        return true;
                }
        }
        return false;
}

static bool _decode_object_fields(
    uint16_t format_version, const uint8_t* bytes, size_t offset,
    const struct executor_format_object_header* object_header,
    const struct executor_format_field* schema, size_t schema_count,
    struct executor_format_object_field** fields) {
        struct executor_format_object_field* result = NULL;
        bool* seen                                  = NULL;
        size_t i;

        if (bytes == NULL || object_header == NULL || fields == NULL ||
            offset > SIZE_MAX - object_header->record_length) {
                return false;
        }
        if (format_version == EXECUTOR_FORMAT_VERSION_COMPACT_OBJECT_LOG) {
                size_t cursor;
                struct executor_format_object_header parsed;

                if (!executor_helper_read_compact_object_header(
                        bytes + offset, object_header->record_length, &parsed,
                        &cursor) ||
                    parsed.object_id != object_header->object_id ||
                    parsed.flags != object_header->flags) {
                        return false;
                }
                result = calloc(schema_count == 0 ? 1 : schema_count,
                                sizeof *result);
                if (result == NULL) {
                        return false;
                }
                for (i = 0; i < schema_count; i++) {
                        if (!executor_helper_read_compact_object_field(
                                bytes + offset, object_header->record_length,
                                &cursor, &schema[i], &result[i])) {
                                _free_object_fields(result, schema_count);
                                return false;
                        }
                }
                if (cursor != object_header->record_length) {
                        _free_object_fields(result, schema_count);
                        return false;
                }
                *fields = result;
                return true;
        }
        if (format_version != EXECUTOR_FORMAT_VERSION_OBJECT_LOG ||
            object_header->field_count != schema_count) {
                return false;
        }
        result = calloc(schema_count == 0 ? 1 : schema_count, sizeof *result);
        seen   = calloc(schema_count == 0 ? 1 : schema_count, sizeof *seen);
        if (result == NULL || seen == NULL) {
                free(result);
                free(seen);
                return false;
        }
        {
                size_t cursor = EXECUTOR_FORMAT_OBJECT_HEADER_SIZE;
                size_t end    = object_header->record_length;
                for (i = 0; i < object_header->field_count; i++) {
                        struct executor_format_object_field_view view;
                        size_t schema_index;
                        uint8_t* value;
                        if (!executor_helper_read_object_field(
                                bytes + offset, object_header->record_length,
                                &cursor, &view) ||
                            !_schema_index_by_id(schema, schema_count,
                                                 view.field_id,
                                                 &schema_index) ||
                            seen[schema_index] ||
                            schema[schema_index].type_code != view.type_code) {
                                free(seen);
                                _free_object_fields(result, schema_count);
                                return false;
                        }
                        value = malloc(
                            view.value_length == 0 ? 1 : view.value_length);
                        if (value == NULL) {
                                free(seen);
                                _free_object_fields(result, schema_count);
                                return false;
                        }
                        memcpy(value, view.value, view.value_length);
                        result[schema_index].field_id     = view.field_id;
                        result[schema_index].type_code    = view.type_code;
                        result[schema_index].value        = value;
                        result[schema_index].value_length = view.value_length;
                        seen[schema_index]                = true;
                }
                if (cursor != end) {
                        free(seen);
                        _free_object_fields(result, schema_count);
                        return false;
                }
        }
        for (i = 0; i < schema_count; i++) {
                if (!seen[i]) {
                        free(seen);
                        _free_object_fields(result, schema_count);
                        return false;
                }
        }
        free(seen);
        *fields = result;
        return true;
}

bool _load_object_fields(struct executor_format_file* file,
                         const struct executor_format_class_entry* entry,
                         uint64_t object_id,
                         const struct executor_format_field* schema,
                         size_t schema_count, uint8_t** segment,
                         size_t* record_offset,
                         struct executor_format_object_header* header,
                         struct executor_format_object_field** fields) {
        struct executor_format_object_header object_header;
        struct executor_format_object_field* result = NULL;
        uint8_t* bytes                              = NULL;
        size_t offset;

        if (!_find_object_in_class(file, entry, object_id, &bytes, &offset,
                                   &object_header) ||
            !_decode_object_fields(file->header.version, bytes, offset,
                                   &object_header, schema, schema_count,
                                   &result)) {
                free(bytes);
                _free_object_fields(result, schema_count);
                return false;
        }
        if (segment != NULL) {
                *segment = bytes;
        } else {
                free(bytes);
        }
        if (record_offset != NULL) {
                *record_offset = offset;
        }
        if (header != NULL) {
                *header = object_header;
        }
        if (fields != NULL) {
                *fields = result;
        } else {
                _free_object_fields(result, schema_count);
        }
        return true;
}

static bool _serialize_object_fields(
    uint16_t format_version, uint64_t object_id, uint16_t flags,
    const struct executor_format_object_field* fields, size_t count,
    uint8_t** bytes, size_t* length) {
        struct executor_format_object_record record;
        size_t capacity;
        size_t i;

        if (format_version == EXECUTOR_FORMAT_VERSION_COMPACT_OBJECT_LOG) {
                if (!executor_helper_compact_object_record_size(
                        object_id, flags, fields, count, &capacity)) {
                        return false;
                }
                *bytes = malloc(capacity == 0 ? 1 : capacity);
                if (*bytes == NULL ||
                    !executor_helper_create_compact_object_record(
                        *bytes, capacity, object_id, flags, fields, count,
                        length)) {
                        free(*bytes);
                        *bytes = NULL;
                        return false;
                }
                return true;
        }
        if (format_version != EXECUTOR_FORMAT_VERSION_CONTIGUOUS &&
            format_version != EXECUTOR_FORMAT_VERSION_OBJECT_LOG) {
                return false;
        }
        if (count > UINT16_MAX ||
            count > (SIZE_MAX - EXECUTOR_FORMAT_OBJECT_HEADER_SIZE) / 9) {
                return false;
        }
        capacity = EXECUTOR_FORMAT_OBJECT_HEADER_SIZE;
        for (i = 0; i < count; i++) {
                size_t field_length = 9 + fields[i].value_length;
                if (field_length > SIZE_MAX - capacity) {
                        return false;
                }
                capacity += field_length;
        }
        *bytes = malloc(capacity);
        if (*bytes == NULL) {
                return false;
        }
        record.object_id   = object_id;
        record.flags       = flags;
        record.fields      = fields;
        record.field_count = (uint16_t)count;
        if (!executor_helper_create_object_record(*bytes, capacity, &record,
                                                  length)) {
                free(*bytes);
                *bytes = NULL;
                return false;
        }
        return true;
}

static enum EXECUTOR_RESULT _set_object(
    struct database* db, const struct analyzer_statement* statement) {
        struct executor_format_file file;
        struct executor_format_class_entry entry;
        struct executor_format_field* schema        = NULL;
        struct executor_format_object_field* fields = NULL;
        struct executor_format_object_header header;
        uint8_t* old_segment      = NULL;
        uint8_t* new_record_bytes = NULL;
        uint8_t* new_segment      = NULL;
        uint64_t object_id;
        size_t schema_count = 0;
        size_t class_index;
        size_t record_offset;
        size_t record_length;
        size_t new_record_length;
        size_t new_segment_length;
        size_t i;
        enum EXECUTOR_RESULT result = EXECUTOR_ERROR_BAD_STATEMENT;

        if (statement->name == NULL || statement->id == NULL ||
            statement->assignments.items == NULL ||
            statement->assignments.count == 0 ||
            !_parse_u64(statement->id, &object_id) || object_id == 0 ||
            !db->format_mode || !executor_format_file_load(db, &file) ||
            !_class_index_by_name(&file, statement->name, &class_index,
                                  &entry) ||
            !_collect_schema_fields(&file, entry.class_id, &schema,
                                    &schema_count) ||
            !_load_object_fields(&file, &entry, object_id, schema, schema_count,
                                 &old_segment, &record_offset, &header,
                                 &fields)) {
                goto done;
        }
        for (i = 0; i < statement->assignments.count; i++) {
                const struct analyzer_assignment* assignment =
                    &statement->assignments.items[i];
                size_t field_index;
                size_t previous;
                uint8_t* value = NULL;
                uint32_t value_length;

                if (assignment->field == NULL ||
                    !_schema_field_index(schema, schema_count,
                                         assignment->field, &field_index)) {
                        goto done;
                }
                for (previous = 0; previous < i; previous++) {
                        if (strcmp(statement->assignments.items[previous].field,
                                   assignment->field) == 0) {
                                goto done;
                        }
                }
                if (!_encode_input_value(&file, &schema[field_index],
                                         &assignment->value, &value,
                                         &value_length)) {
                        free(value);
                        goto done;
                }
                free((void*)fields[field_index].value);
                fields[field_index].value        = value;
                fields[field_index].value_length = value_length;
        }
        if (!_serialize_object_fields(file.header.version, object_id, 0, fields,
                                      schema_count, &new_record_bytes,
                                      &new_record_length)) {
                result = EXECUTOR_ERROR_STORAGE_FAILED;
                goto done;
        }
        if (file.header.version >= EXECUTOR_FORMAT_VERSION_OBJECT_LOG) {
                if (!_append_object_log_version(&file, class_index, &entry,
                                                new_record_bytes,
                                                new_record_length)) {
                        result = EXECUTOR_ERROR_SAVE_FAILED;
                        goto done;
                }
                result = EXECUTOR_OK;
                goto done;
        }
        record_length = header.record_length;
        if (entry.data_length > SIZE_MAX ||
            record_offset > (size_t)entry.data_length ||
            record_length > (size_t)entry.data_length - record_offset ||
            new_record_length >
                SIZE_MAX - ((size_t)entry.data_length - record_length)) {
                result = EXECUTOR_ERROR_STORAGE_FAILED;
                goto done;
        }
        new_segment_length =
            (size_t)entry.data_length - record_length + new_record_length;
        new_segment = malloc(new_segment_length == 0 ? 1 : new_segment_length);
        if (new_segment == NULL) {
                result = EXECUTOR_ERROR_STORAGE_FAILED;
                goto done;
        }
        memcpy(new_segment, old_segment, record_offset);
        memcpy(new_segment + record_offset, new_record_bytes,
               new_record_length);
        memcpy(new_segment + record_offset + new_record_length,
               old_segment + record_offset + record_length,
               (size_t)entry.data_length - record_offset - record_length);
        if (!_rewrite_object_segment(&file, class_index, &entry, new_segment,
                                     new_segment_length) ||
            !executor_format_file_commit(&file)) {
                result = EXECUTOR_ERROR_SAVE_FAILED;
                goto done;
        }
        result = EXECUTOR_OK;
done:
        free(old_segment);
        free(new_record_bytes);
        free(new_segment);
        _free_object_fields(fields, schema_count);
        _free_schema_fields(schema, schema_count);
        return result;
}

static bool _object_is_referenced(struct executor_format_file* file,
                                  uint32_t target_class_id,
                                  uint64_t target_object_id) {
        size_t i;

        for (i = 0; i < file->header.class_count; i++) {
                struct executor_format_class_entry entry;
                uint8_t* segment = NULL;
                size_t length;
                size_t offset = 0;

                if (!executor_format_file_read_class_entry(file, i, &entry)) {
                        return true;
                }
                if (file->header.version >=
                    EXECUTOR_FORMAT_VERSION_OBJECT_LOG) {
                        struct executor_format_field* schema = NULL;
                        size_t schema_count                  = 0;
                        struct object_log_record* records    = NULL;
                        size_t record_count                  = 0;
                        size_t record_index;

                        if (!_collect_schema_fields(file, entry.class_id,
                                                    &schema, &schema_count) ||
                            !_collect_object_log_records(file, &entry, &records,
                                                         &record_count)) {
                                _free_schema_fields(schema, schema_count);
                                _free_object_log_records(records, record_count);
                                return true;
                        }
                        for (record_index = 0; record_index < record_count;
                             record_index++) {
                                struct object_log_record* record =
                                    &records[record_index];
                                struct executor_format_object_header header;
                                struct executor_format_object_field* fields =
                                    NULL;
                                size_t field_index;

                                if (record->deleted) {
                                        continue;
                                }
                                if (!_read_object_record_header(
                                        file->header.version, record->bytes,
                                        record->length, &header) ||
                                    !_decode_object_fields(
                                        file->header.version, record->bytes, 0,
                                        &header, schema, schema_count,
                                        &fields)) {
                                        _free_object_fields(fields,
                                                            schema_count);
                                        _free_schema_fields(schema,
                                                            schema_count);
                                        _free_object_log_records(records,
                                                                 record_count);
                                        return true;
                                }
                                for (field_index = 0;
                                     field_index < schema_count;
                                     field_index++) {
                                        if (fields[field_index].type_code ==
                                                EXECUTOR_FORMAT_TYPE_POINTER &&
                                            executor_helper_read_u32(
                                                fields[field_index].value) ==
                                                target_class_id &&
                                            executor_helper_read_u64(
                                                fields[field_index].value +
                                                4) == target_object_id &&
                                            !(entry.class_id ==
                                                  target_class_id &&
                                              record->object_id ==
                                                  target_object_id)) {
                                                _free_object_fields(
                                                    fields, schema_count);
                                                _free_schema_fields(
                                                    schema, schema_count);
                                                _free_object_log_records(
                                                    records, record_count);
                                                return true;
                                        }
                                }
                                _free_object_fields(fields, schema_count);
                        }
                        _free_schema_fields(schema, schema_count);
                        _free_object_log_records(records, record_count);
                        continue;
                }
                if (entry.data_length > SIZE_MAX ||
                    !executor_helper_range_valid(file->size, entry.data_offset,
                                                 entry.data_length)) {
                        return true;
                }
                length = (size_t)entry.data_length;
                if (length != 0) {
                        segment = malloc(length);
                        if (segment == NULL ||
                            !executor_format_file_read(file, entry.data_offset,
                                                       segment, length)) {
                                free(segment);
                                return true;
                        }
                }
                while (offset < length) {
                        struct executor_format_object_header header;
                        size_t cursor;
                        size_t field_index;

                        if (!executor_helper_read_object_header(
                                segment + offset, length - offset, &header) ||
                            header.record_length > length - offset) {
                                free(segment);
                                return true;
                        }
                        cursor = EXECUTOR_FORMAT_OBJECT_HEADER_SIZE;
                        for (field_index = 0; field_index < header.field_count;
                             field_index++) {
                                struct executor_format_object_field_view field;
                                if (!executor_helper_read_object_field(
                                        segment + offset, header.record_length,
                                        &cursor, &field)) {
                                        free(segment);
                                        return true;
                                }
                                if (field.type_code ==
                                        EXECUTOR_FORMAT_TYPE_POINTER &&
                                    executor_helper_read_u32(field.value) ==
                                        target_class_id &&
                                    executor_helper_read_u64(field.value + 4) ==
                                        target_object_id &&
                                    !(entry.class_id == target_class_id &&
                                      header.object_id == target_object_id)) {
                                        free(segment);
                                        return true;
                                }
                        }
                        offset += header.record_length;
                }
                free(segment);
        }
        return false;
}

static enum EXECUTOR_RESULT _delete_object(
    struct database* db, const struct analyzer_statement* statement) {
        struct executor_format_file file;
        struct executor_format_class_entry entry;
        struct executor_format_field* schema               = NULL;
        struct executor_format_object_field* unused_fields = NULL;
        struct executor_format_object_header header;
        uint8_t* old_segment = NULL;
        uint8_t* new_segment = NULL;
        uint8_t* tombstone   = NULL;
        uint64_t object_id;
        size_t schema_count = 0;
        size_t class_index;
        size_t record_offset;
        size_t new_segment_length;
        enum EXECUTOR_RESULT result = EXECUTOR_ERROR_BAD_STATEMENT;

        if (statement->name == NULL || statement->id == NULL ||
            !_parse_u64(statement->id, &object_id) || object_id == 0 ||
            !db->format_mode || !executor_format_file_load(db, &file) ||
            !_class_index_by_name(&file, statement->name, &class_index,
                                  &entry) ||
            !_collect_schema_fields(&file, entry.class_id, &schema,
                                    &schema_count) ||
            !_load_object_fields(&file, &entry, object_id, schema, schema_count,
                                 &old_segment, &record_offset, &header,
                                 &unused_fields)) {
                goto done;
        }
        if (_object_is_referenced(&file, entry.class_id, object_id) ||
            entry.object_count == 0) {
                goto done;
        }
        if (file.header.version >= EXECUTOR_FORMAT_VERSION_OBJECT_LOG) {
                size_t tombstone_length;

                if (!_serialize_object_fields(
                        file.header.version, object_id,
                        EXECUTOR_FORMAT_OBJECT_FLAG_DELETED, NULL, 0,
                        &tombstone, &tombstone_length)) {
                        result = EXECUTOR_ERROR_STORAGE_FAILED;
                        goto done;
                }
                entry.object_count--;
                if (!_append_object_log_version(&file, class_index, &entry,
                                                tombstone, tombstone_length)) {
                        result = EXECUTOR_ERROR_SAVE_FAILED;
                        goto done;
                }
                result = EXECUTOR_OK;
                goto done;
        }
        if (entry.data_length > SIZE_MAX ||
            record_offset > (size_t)entry.data_length ||
            header.record_length > (size_t)entry.data_length - record_offset) {
                result = EXECUTOR_ERROR_STORAGE_FAILED;
                goto done;
        }
        new_segment_length = (size_t)entry.data_length - header.record_length;
        new_segment = malloc(new_segment_length == 0 ? 1 : new_segment_length);
        if (new_segment == NULL) {
                result = EXECUTOR_ERROR_STORAGE_FAILED;
                goto done;
        }
        memcpy(new_segment, old_segment, record_offset);
        memcpy(
            new_segment + record_offset,
            old_segment + record_offset + header.record_length,
            (size_t)entry.data_length - record_offset - header.record_length);
        entry.object_count--;
        if (!_rewrite_object_segment(&file, class_index, &entry, new_segment,
                                     new_segment_length) ||
            !executor_format_file_commit(&file)) {
                result = EXECUTOR_ERROR_SAVE_FAILED;
                goto done;
        }
        result = EXECUTOR_OK;
done:
        free(old_segment);
        free(new_segment);
        free(tombstone);
        _free_object_fields(unused_fields, schema_count);
        _free_schema_fields(schema, schema_count);
        return result;
}

static enum EXECUTOR_RESULT _find_objects(
    struct database* db, const struct analyzer_statement* statement,
    FILE* output) {
        struct executor_format_file file;
        struct executor_format_class_entry entry;
        struct executor_format_field* schema = NULL;
        uint8_t* segment                     = NULL;
        struct object_log_record* records    = NULL;
        size_t record_count                  = 0;
        size_t schema_count                  = 0;
        size_t length                        = 0;
        size_t offset                        = 0;
        enum EXECUTOR_RESULT result          = EXECUTOR_ERROR_STORAGE_FAILED;

        if (statement->name == NULL || !db->format_mode ||
            !executor_format_file_load(db, &file)) {
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }
        if (!_find_class(&file, statement->name, &entry)) {
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }
        if (!_collect_schema_fields(&file, entry.class_id, &schema,
                                    &schema_count)) {
                _free_schema_fields(schema, schema_count);
                return EXECUTOR_ERROR_STORAGE_FAILED;
        }
        if (file.header.version >= EXECUTOR_FORMAT_VERSION_OBJECT_LOG) {
                size_t record_index;

                if (!_collect_object_log_records(&file, &entry, &records,
                                                 &record_count)) {
                        result = EXECUTOR_ERROR_STORAGE_FAILED;
                        goto done;
                }
                for (record_index = 0; record_index < record_count;
                     record_index++) {
                        struct object_log_record* record =
                            &records[record_index];
                        struct executor_format_object_header header;
                        struct executor_format_object_field* fields = NULL;
                        size_t field_index;
                        struct executor_value condition;
                        enum EXECUTOR_RESULT print_result;
                        bool matches = true;

                        if (record->deleted ||
                            !_read_object_record_header(
                                file.header.version, record->bytes,
                                record->length, &header) ||
                            !_decode_object_fields(
                                file.header.version, record->bytes, 0, &header,
                                schema, schema_count, &fields)) {
                                if (!record->deleted) {
                                        result = EXECUTOR_ERROR_STORAGE_FAILED;
                                        goto done;
                                }
                                continue;
                        }
                        if (statement->expression != NULL) {
                                if (!_evaluate_ast(statement->expression,
                                                   schema, schema_count, fields,
                                                   &condition) ||
                                    condition.type_code !=
                                        EXECUTOR_FORMAT_TYPE_BOOL) {
                                        _free_object_fields(fields,
                                                            schema_count);
                                        result = EXECUTOR_ERROR_BAD_STATEMENT;
                                        goto done;
                                }
                                matches = condition.value.boolean;
                        }
                        if (matches) {
                                if (fprintf(
                                        output, "%s.%llu", statement->name,
                                        (unsigned long long)header.object_id) <
                                    0) {
                                        _free_object_fields(fields,
                                                            schema_count);
                                        result = EXECUTOR_ERROR_OUTPUT_FAILED;
                                        goto done;
                                }
                                for (field_index = 0;
                                     field_index < schema_count;
                                     field_index++) {
                                        struct executor_value value;
                                        if (!_value_from_bytes(
                                                fields[field_index].type_code,
                                                fields[field_index].value,
                                                fields[field_index]
                                                    .value_length,
                                                &value)) {
                                                _free_object_fields(
                                                    fields, schema_count);
                                                result =
                                                    EXECUTOR_ERROR_STORAGE_FAILED;
                                                goto done;
                                        }
                                        if (fprintf(output, " %s=",
                                                    schema[field_index].name) <
                                            0) {
                                                _free_object_fields(
                                                    fields, schema_count);
                                                result =
                                                    EXECUTOR_ERROR_OUTPUT_FAILED;
                                                goto done;
                                        }
                                        print_result =
                                            _print_value(output, &file, &value);
                                        if (print_result != EXECUTOR_OK) {
                                                _free_object_fields(
                                                    fields, schema_count);
                                                result = print_result;
                                                goto done;
                                        }
                                }
                                if (fputc('\n', output) == EOF) {
                                        _free_object_fields(fields,
                                                            schema_count);
                                        result = EXECUTOR_ERROR_OUTPUT_FAILED;
                                        goto done;
                                }
                        }
                        _free_object_fields(fields, schema_count);
                }
                result = EXECUTOR_OK;
                goto done;
        }
        if (entry.data_length > SIZE_MAX ||
            !executor_helper_range_valid(file.size, entry.data_offset,
                                         entry.data_length)) {
                result = EXECUTOR_ERROR_STORAGE_FAILED;
                goto done;
        }
        length = (size_t)entry.data_length;
        if (length != 0) {
                segment = malloc(length);
                if (segment == NULL ||
                    !executor_format_file_read(&file, entry.data_offset,
                                               segment, length)) {
                        free(segment);
                        _free_schema_fields(schema, schema_count);
                        return EXECUTOR_ERROR_STORAGE_FAILED;
                }
        }
        while (offset < length) {
                struct executor_format_object_header header;
                struct executor_format_object_field* fields = NULL;
                uint8_t* object_segment                     = NULL;
                struct executor_value condition;
                enum EXECUTOR_RESULT print_result;
                size_t i;
                bool matches = true;

                if (!executor_helper_read_object_header(
                        segment + offset, length - offset, &header) ||
                    header.record_length > length - offset ||
                    !_load_object_fields(&file, &entry, header.object_id,
                                         schema, schema_count, &object_segment,
                                         NULL, &header, &fields)) {
                        result = EXECUTOR_ERROR_STORAGE_FAILED;
                        goto done;
                }
                free(object_segment);
                if (statement->expression != NULL) {
                        if (!_evaluate_ast(statement->expression, schema,
                                           schema_count, fields, &condition) ||
                            condition.type_code != EXECUTOR_FORMAT_TYPE_BOOL) {
                                _free_object_fields(fields, schema_count);
                                result = EXECUTOR_ERROR_BAD_STATEMENT;
                                goto done;
                        }
                        matches = condition.value.boolean;
                }
                if (matches) {
                        if (fprintf(output, "%s.%llu", statement->name,
                                    (unsigned long long)header.object_id) < 0) {
                                _free_object_fields(fields, schema_count);
                                result = EXECUTOR_ERROR_OUTPUT_FAILED;
                                goto done;
                        }
                        for (i = 0; i < schema_count; i++) {
                                struct executor_value value;
                                if (!_value_from_bytes(
                                        fields[i].type_code, fields[i].value,
                                        fields[i].value_length, &value)) {
                                        _free_object_fields(fields,
                                                            schema_count);
                                        result = EXECUTOR_ERROR_STORAGE_FAILED;
                                        goto done;
                                }
                                if (fprintf(output, " %s=", schema[i].name) <
                                    0) {
                                        _free_object_fields(fields,
                                                            schema_count);
                                        result = EXECUTOR_ERROR_OUTPUT_FAILED;
                                        goto done;
                                }
                                print_result =
                                    _print_value(output, &file, &value);
                                if (print_result != EXECUTOR_OK) {
                                        _free_object_fields(fields,
                                                            schema_count);
                                        result = print_result;
                                        goto done;
                                }
                        }
                        if (fputc('\n', output) == EOF) {
                                _free_object_fields(fields, schema_count);
                                result = EXECUTOR_ERROR_OUTPUT_FAILED;
                                goto done;
                        }
                }
                _free_object_fields(fields, schema_count);
                offset += header.record_length;
        }
        result = EXECUTOR_OK;
done:
        free(segment);
        _free_object_log_records(records, record_count);
        _free_schema_fields(schema, schema_count);
        return result;
}

enum EXECUTOR_RESULT executor_objects_execute(
    struct database* db, const struct analyzer_statement* statement,
    FILE* output) {
        switch (statement->kind) {
                case ANALYZER_NEW:
                        return _new_object(db, statement, output);
                case ANALYZER_FIND:
                        return _find_objects(db, statement, output);
                case ANALYZER_SET:
                        return _set_object(db, statement);
                case ANALYZER_DELETE:
                        return _delete_object(db, statement);
                default:
                        return EXECUTOR_ERROR_NOT_IMPLEMENTED;
        }
}
