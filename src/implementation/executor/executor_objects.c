/**
 * @file executor_objects.c
 * @brief Executor objects implementation.
 */

#include "implementation/executor/executor_internal.h"

static bool _find_object_in_class(
    struct executor_format_file* file,
    const struct executor_format_class_entry* entry, uint64_t object_id,
    uint8_t** segment_bytes, size_t* record_offset,
    struct executor_format_object_header* header) {
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

static enum EXECUTOR_RESULT _new_object(
    struct database* db, const struct analyzer_statement* statement) {
        struct executor_format_file file;
        struct executor_format_class_entry entry;
        struct executor_format_field* schema               = NULL;
        struct executor_format_object_field* record_fields = NULL;
        struct executor_format_object_record record;
        uint8_t* record_bytes = NULL;
        uint8_t* old_segment  = NULL;
        uint8_t* segment      = NULL;
        size_t schema_count   = 0;
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
        record.object_id   = object_id;
        record.flags       = 0;
        record.fields      = record_fields;
        record.field_count = (uint16_t)schema_count;
        record_length      = 16;
        for (i = 0; i < schema_count; i++) {
                size_t add = 9 + record_fields[i].value_length;
                if (add > SIZE_MAX - record_length) {
                        goto done;
                }
                record_length += add;
        }
        record_bytes = malloc(record_length);
        if (record_bytes == NULL ||
            !executor_helper_create_object_record(record_bytes, record_length,
                                                  &record, &record_length)) {
                result = EXECUTOR_ERROR_STORAGE_FAILED;
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
        printf("%s.%llu\n", statement->name, (unsigned long long)object_id);
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
        bool* seen                                  = NULL;
        size_t offset;
        size_t i;

        if (!_find_object_in_class(file, entry, object_id, &bytes, &offset,
                                   &object_header) ||
            object_header.field_count != schema_count) {
                free(bytes);
                return false;
        }
        result = calloc(schema_count == 0 ? 1 : schema_count, sizeof *result);
        seen   = calloc(schema_count == 0 ? 1 : schema_count, sizeof *seen);
        if (result == NULL || seen == NULL) {
                free(bytes);
                free(result);
                free(seen);
                return false;
        }
        {
                size_t cursor = EXECUTOR_FORMAT_OBJECT_HEADER_SIZE;
                size_t end    = object_header.record_length;
                for (i = 0; i < object_header.field_count; i++) {
                        struct executor_format_object_field_view view;
                        size_t schema_index;
                        uint8_t* value;
                        if (!executor_helper_read_object_field(
                                bytes + offset, object_header.record_length,
                                &cursor, &view) ||
                            !_schema_index_by_id(schema, schema_count,
                                                 view.field_id,
                                                 &schema_index) ||
                            seen[schema_index] ||
                            schema[schema_index].type_code != view.type_code) {
                                free(bytes);
                                free(seen);
                                _free_object_fields(result, schema_count);
                                return false;
                        }
                        value = malloc(
                            view.value_length == 0 ? 1 : view.value_length);
                        if (value == NULL) {
                                free(bytes);
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
                        free(bytes);
                        free(seen);
                        _free_object_fields(result, schema_count);
                        return false;
                }
        }
        for (i = 0; i < schema_count; i++) {
                if (!seen[i]) {
                        free(bytes);
                        free(seen);
                        _free_object_fields(result, schema_count);
                        return false;
                }
        }
        free(seen);
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
    uint64_t object_id, const struct executor_format_object_field* fields,
    size_t count, uint8_t** bytes, size_t* length) {
        struct executor_format_object_record record;
        size_t capacity = EXECUTOR_FORMAT_OBJECT_HEADER_SIZE;
        size_t i;

        if (count > UINT16_MAX) {
                return false;
        }
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
        record.flags       = 0;
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
        if (!_serialize_object_fields(object_id, fields, schema_count,
                                      &new_record_bytes, &new_record_length)) {
                result = EXECUTOR_ERROR_STORAGE_FAILED;
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

                if (!executor_format_file_read_class_entry(file, i, &entry) ||
                    entry.data_length > SIZE_MAX ||
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
            entry.object_count == 0 ||
            header.record_length > (size_t)entry.data_length - record_offset) {
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
        _free_object_fields(unused_fields, schema_count);
        _free_schema_fields(schema, schema_count);
        return result;
}

static enum EXECUTOR_RESULT _find_objects(
    struct database* db, const struct analyzer_statement* statement) {
        struct executor_format_file file;
        struct executor_format_class_entry entry;
        struct executor_format_field* schema = NULL;
        uint8_t* segment                     = NULL;
        size_t schema_count                  = 0;
        size_t length;
        size_t offset               = 0;
        enum EXECUTOR_RESULT result = EXECUTOR_ERROR_STORAGE_FAILED;

        if (statement->name == NULL || !db->format_mode ||
            !executor_format_file_load(db, &file)) {
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }
        if (!_find_class(&file, statement->name, &entry)) {
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }
        if (!_collect_schema_fields(&file, entry.class_id, &schema,
                                    &schema_count) ||
            entry.data_length > SIZE_MAX ||
            !executor_helper_range_valid(file.size, entry.data_offset,
                                         entry.data_length)) {
                _free_schema_fields(schema, schema_count);
                return EXECUTOR_ERROR_STORAGE_FAILED;
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
                        if (printf("%s.%llu", statement->name,
                                   (unsigned long long)header.object_id) < 0) {
                                _free_object_fields(fields, schema_count);
                                result = EXECUTOR_ERROR_STORAGE_FAILED;
                                goto done;
                        }
                        for (i = 0; i < schema_count; i++) {
                                struct executor_value value;
                                if (!_value_from_bytes(
                                        fields[i].type_code, fields[i].value,
                                        fields[i].value_length, &value) ||
                                    printf(" %s=", schema[i].name) < 0 ||
                                    !_print_value(&file, &value)) {
                                        _free_object_fields(fields,
                                                            schema_count);
                                        result = EXECUTOR_ERROR_STORAGE_FAILED;
                                        goto done;
                                }
                        }
                        if (putchar('\n') == EOF) {
                                _free_object_fields(fields, schema_count);
                                result = EXECUTOR_ERROR_STORAGE_FAILED;
                                goto done;
                        }
                }
                _free_object_fields(fields, schema_count);
                offset += header.record_length;
        }
        result = EXECUTOR_OK;
done:
        free(segment);
        _free_schema_fields(schema, schema_count);
        return result;
}

enum EXECUTOR_RESULT executor_objects_execute(
    struct database* db, const struct analyzer_statement* statement) {
        switch (statement->kind) {
                case ANALYZER_NEW:
                        return _new_object(db, statement);
                case ANALYZER_FIND:
                        return _find_objects(db, statement);
                case ANALYZER_SET:
                        return _set_object(db, statement);
                case ANALYZER_DELETE:
                        return _delete_object(db, statement);
                default:
                        return EXECUTOR_ERROR_NOT_IMPLEMENTED;
        }
}
