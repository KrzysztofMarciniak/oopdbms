/**
 * @file create_object_record.c
 * @brief Create and read object record headers and field records.
 */

#include "implementation/executor/helper/create_object_record.h"

#include "implementation/executor/helper/binary_encoding.h"

static bool _valid_value(const struct executor_format_object_field* field) {
        if (field->value_length != 0 && field->value == NULL) {
                return false;
        }
        switch (field->type_code) {
                case EXECUTOR_FORMAT_TYPE_INTEGER:
                case EXECUTOR_FORMAT_TYPE_FLOAT:
                        return field->value_length == 8;
                case EXECUTOR_FORMAT_TYPE_TEXT:
                        return true;
                case EXECUTOR_FORMAT_TYPE_BOOL:
                        return field->value_length == 1 && field->value[0] <= 1;
                case EXECUTOR_FORMAT_TYPE_POINTER:
                        return field->value_length == 12;
                default:
                        return false;
        }
}

bool executor_helper_create_object_record(
    uint8_t* output, size_t capacity,
    const struct executor_format_object_record* record, size_t* written) {
        uint64_t record_length = EXECUTOR_FORMAT_OBJECT_HEADER_SIZE;
        uint64_t next_length;
        size_t offset = EXECUTOR_FORMAT_OBJECT_HEADER_SIZE;
        size_t i;

        if (output == NULL || record == NULL || written == NULL ||
            (record->field_count != 0 && record->fields == NULL)) {
                return false;
        }
        for (i = 0; i < record->field_count; i++) {
                const struct executor_format_object_field* field =
                    &record->fields[i];

                if (!_valid_value(field) ||
                    !executor_helper_add_u64(record_length, 9, &next_length) ||
                    !executor_helper_add_u64(next_length, field->value_length,
                                             &record_length)) {
                        return false;
                }
        }
        if (record_length > UINT32_MAX || capacity < record_length) {
                return false;
        }
        executor_helper_write_u32(output, (uint32_t)record_length);
        executor_helper_write_u64(output + 4, record->object_id);
        executor_helper_write_u16(output + 12, record->flags);
        executor_helper_write_u16(output + 14, record->field_count);
        for (i = 0; i < record->field_count; i++) {
                const struct executor_format_object_field* field =
                    &record->fields[i];

                executor_helper_write_u32(output + offset, field->field_id);
                output[offset + 4] = field->type_code;
                executor_helper_write_u32(output + offset + 5,
                                          field->value_length);
                offset += 9;
                for (size_t value_index = 0; value_index < field->value_length;
                     value_index++) {
                        output[offset + value_index] =
                            field->value[value_index];
                }
                offset += field->value_length;
        }
        *written = offset;
        return true;
}

bool executor_helper_read_object_header(
    const uint8_t* bytes, size_t length,
    struct executor_format_object_header* header) {
        uint32_t record_length;

        if (bytes == NULL || header == NULL ||
            length < EXECUTOR_FORMAT_OBJECT_HEADER_SIZE) {
                return false;
        }
        record_length = executor_helper_read_u32(bytes);
        if (record_length < EXECUTOR_FORMAT_OBJECT_HEADER_SIZE ||
            record_length > length) {
                return false;
        }
        header->record_length = record_length;
        header->object_id     = executor_helper_read_u64(bytes + 4);
        header->flags         = executor_helper_read_u16(bytes + 12);
        header->field_count   = executor_helper_read_u16(bytes + 14);
        return true;
}

bool executor_helper_read_object_field(
    const uint8_t* bytes, size_t record_length, size_t* offset,
    struct executor_format_object_field_view* field) {
        size_t start;
        uint32_t value_length;

        if (bytes == NULL || offset == NULL || field == NULL ||
            record_length < EXECUTOR_FORMAT_OBJECT_HEADER_SIZE ||
            *offset < EXECUTOR_FORMAT_OBJECT_HEADER_SIZE ||
            *offset > record_length || record_length - *offset < 9) {
                return false;
        }
        start        = *offset;
        value_length = executor_helper_read_u32(bytes + start + 5);
        if ((uint64_t)value_length > record_length - start - 9) {
                return false;
        }
        field->field_id     = executor_helper_read_u32(bytes + start);
        field->type_code    = bytes[start + 4];
        field->value_length = value_length;
        field->value        = bytes + start + 9;
        {
                const struct executor_format_object_field value = {
                    .field_id     = field->field_id,
                    .type_code    = field->type_code,
                    .value        = field->value,
                    .value_length = field->value_length,
                };

                if (!_valid_value(&value)) {
                        return false;
                }
        }
        *offset += 9 + (size_t)value_length;
        return true;
}
