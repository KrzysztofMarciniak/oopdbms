/**
 * @file create_object_record.c
 * @brief Create and read object record headers and field records.
 */

#include "implementation/executor/helper/create_object_record.h"

#include <stdlib.h>
#include <string.h>

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

static int64_t _read_i64_value(const uint8_t* bytes) {
        uint64_t encoded = executor_helper_read_u64(bytes);

        if (encoded <= INT64_MAX) {
                return (int64_t)encoded;
        }
        return -1 - (int64_t)(UINT64_MAX - encoded);
}

static bool _compact_field_size(
    const struct executor_format_object_field* field, size_t* size) {
        uint64_t class_id;
        uint64_t object_id;
        size_t encoded_size;

        if (!_valid_value(field) || size == NULL) {
                return false;
        }
        switch (field->type_code) {
                case EXECUTOR_FORMAT_TYPE_INTEGER:
                        *size = executor_helper_svarint_size(
                            _read_i64_value(field->value));
                        return true;
                case EXECUTOR_FORMAT_TYPE_FLOAT:
                        *size = 8;
                        return true;
                case EXECUTOR_FORMAT_TYPE_TEXT:
                        encoded_size =
                            executor_helper_varint_size(field->value_length);
                        if (field->value_length > SIZE_MAX - encoded_size) {
                                return false;
                        }
                        *size = encoded_size + field->value_length;
                        return true;
                case EXECUTOR_FORMAT_TYPE_BOOL:
                        *size = 1;
                        return true;
                case EXECUTOR_FORMAT_TYPE_POINTER:
                        class_id  = executor_helper_read_u32(field->value);
                        object_id = executor_helper_read_u64(field->value + 4);
                        if (class_id == 0 || object_id == 0) {
                                return false;
                        }
                        encoded_size = executor_helper_varint_size(object_id);
                        if (encoded_size > SIZE_MAX - 4) {
                                return false;
                        }
                        *size = 4 + encoded_size;
                        return true;
                default:
                        return false;
        }
}

bool executor_helper_compact_object_record_size(
    uint64_t object_id, uint16_t flags,
    const struct executor_format_object_field* fields, size_t field_count,
    size_t* size) {
        size_t total;
        size_t i;

        if (object_id == 0 ||
            (flags & ~EXECUTOR_FORMAT_OBJECT_FLAG_DELETED) != 0 ||
            (field_count != 0 && fields == NULL) ||
            ((flags & EXECUTOR_FORMAT_OBJECT_FLAG_DELETED) != 0 &&
             field_count != 0) ||
            field_count > UINT16_MAX || size == NULL) {
                return false;
        }
        total = executor_helper_varint_size(object_id) + 1;
        for (i = 0; i < field_count; i++) {
                size_t field_size;

                if (!_compact_field_size(&fields[i], &field_size) ||
                    field_size > SIZE_MAX - total) {
                        return false;
                }
                total += field_size;
        }
        if (total > UINT32_MAX) {
                return false;
        }
        *size = total;
        return true;
}

bool executor_helper_create_compact_object_record(
    uint8_t* output, size_t capacity, uint64_t object_id, uint16_t flags,
    const struct executor_format_object_field* fields, size_t field_count,
    size_t* written) {
        size_t expected_size;
        size_t offset = 0;
        size_t i;

        if (output == NULL || written == NULL ||
            !executor_helper_compact_object_record_size(
                object_id, flags, fields, field_count, &expected_size) ||
            capacity < expected_size ||
            !executor_helper_write_varint(output, capacity, &offset,
                                          object_id)) {
                return false;
        }
        output[offset++] = (uint8_t)flags;
        for (i = 0; i < field_count; i++) {
                const struct executor_format_object_field* field = &fields[i];

                switch (field->type_code) {
                        case EXECUTOR_FORMAT_TYPE_INTEGER:
                                if (!executor_helper_write_svarint(
                                        output, capacity, &offset,
                                        _read_i64_value(field->value))) {
                                        return false;
                                }
                                break;
                        case EXECUTOR_FORMAT_TYPE_FLOAT:
                                memcpy(output + offset, field->value, 8);
                                offset += 8;
                                break;
                        case EXECUTOR_FORMAT_TYPE_TEXT:
                                if (!executor_helper_write_varint(
                                        output, capacity, &offset,
                                        field->value_length)) {
                                        return false;
                                }
                                memcpy(output + offset, field->value,
                                       field->value_length);
                                offset += field->value_length;
                                break;
                        case EXECUTOR_FORMAT_TYPE_BOOL:
                                output[offset++] = field->value[0];
                                break;
                        case EXECUTOR_FORMAT_TYPE_POINTER:
                                memcpy(output + offset, field->value, 4);
                                offset += 4;
                                if (!executor_helper_write_varint(
                                        output, capacity, &offset,
                                        executor_helper_read_u64(field->value +
                                                                 4))) {
                                        return false;
                                }
                                break;
                        default:
                                return false;
                }
        }
        *written = offset;
        return offset == expected_size;
}

bool executor_helper_read_compact_object_header(
    const uint8_t* bytes, size_t record_length,
    struct executor_format_object_header* header, size_t* fields_offset) {
        size_t offset = 0;
        uint64_t object_id;
        uint8_t flags;

        if (bytes == NULL || header == NULL || fields_offset == NULL ||
            record_length > UINT32_MAX ||
            !executor_helper_read_varint(bytes, record_length, &offset,
                                         &object_id) ||
            object_id == 0 || offset >= record_length) {
                return false;
        }
        flags = bytes[offset++];
        if ((flags & ~EXECUTOR_FORMAT_OBJECT_FLAG_DELETED) != 0 ||
            ((flags & EXECUTOR_FORMAT_OBJECT_FLAG_DELETED) != 0 &&
             offset != record_length)) {
                return false;
        }
        header->record_length = (uint32_t)record_length;
        header->object_id     = object_id;
        header->flags         = flags;
        header->field_count   = 0;
        *fields_offset        = offset;
        return true;
}

bool executor_helper_read_compact_object_field(
    const uint8_t* bytes, size_t record_length, size_t* offset,
    const struct executor_format_field* schema_field,
    struct executor_format_object_field* field) {
        size_t cursor;
        uint8_t* value = NULL;
        uint32_t value_length;

        if (bytes == NULL || offset == NULL || schema_field == NULL ||
            field == NULL || *offset > record_length) {
                return false;
        }
        cursor = *offset;
        switch (schema_field->type_code) {
                case EXECUTOR_FORMAT_TYPE_INTEGER: {
                        int64_t decoded;
                        if (!executor_helper_read_svarint(bytes, record_length,
                                                          &cursor, &decoded)) {
                                return false;
                        }
                        value = malloc(8);
                        if (value == NULL) {
                                return false;
                        }
                        executor_helper_write_u64(value, (uint64_t)decoded);
                        value_length = 8;
                        break;
                }
                case EXECUTOR_FORMAT_TYPE_FLOAT:
                        if (record_length - cursor < 8) {
                                return false;
                        }
                        value = malloc(8);
                        if (value == NULL) {
                                return false;
                        }
                        memcpy(value, bytes + cursor, 8);
                        cursor += 8;
                        value_length = 8;
                        break;
                case EXECUTOR_FORMAT_TYPE_TEXT: {
                        uint64_t decoded_length;
                        if (!executor_helper_read_varint(bytes, record_length,
                                                         &cursor,
                                                         &decoded_length) ||
                            decoded_length > UINT32_MAX ||
                            decoded_length > record_length - cursor) {
                                return false;
                        }
                        value_length = (uint32_t)decoded_length;
                        value = malloc(value_length == 0 ? 1 : value_length);
                        if (value == NULL) {
                                return false;
                        }
                        memcpy(value, bytes + cursor, value_length);
                        cursor += value_length;
                        break;
                }
                case EXECUTOR_FORMAT_TYPE_BOOL:
                        if (cursor >= record_length || bytes[cursor] > 1) {
                                return false;
                        }
                        value = malloc(1);
                        if (value == NULL) {
                                return false;
                        }
                        value[0]     = bytes[cursor++];
                        value_length = 1;
                        break;
                case EXECUTOR_FORMAT_TYPE_POINTER: {
                        uint32_t class_id;
                        uint64_t object_id;
                        if (record_length - cursor < 4) {
                                return false;
                        }
                        class_id = executor_helper_read_u32(bytes + cursor);
                        cursor += 4;
                        if (class_id == 0 ||
                            !executor_helper_read_varint(bytes, record_length,
                                                         &cursor, &object_id) ||
                            object_id == 0) {
                                return false;
                        }
                        value = malloc(12);
                        if (value == NULL) {
                                return false;
                        }
                        executor_helper_write_u32(value, class_id);
                        executor_helper_write_u64(value + 4, object_id);
                        value_length = 12;
                        break;
                }
                default:
                        return false;
        }
        field->field_id     = schema_field->field_id;
        field->type_code    = schema_field->type_code;
        field->value        = value;
        field->value_length = value_length;
        *offset             = cursor;
        return true;
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
