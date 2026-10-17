/**
 * @file create_field_descriptor.c
 * @brief Create and read one variable-length schema field descriptor.
 */

#include "implementation/executor/helper/create_field_descriptor.h"

#include "implementation/executor/helper/binary_encoding.h"

static bool _valid_type(uint8_t type_code) {
        return type_code >= EXECUTOR_FORMAT_TYPE_INTEGER &&
               type_code <= EXECUTOR_FORMAT_TYPE_POINTER;
}

bool executor_helper_field_descriptor_size(
    const struct executor_format_field* field, size_t* size) {
        size_t fixed_size;

        if (field == NULL || size == NULL || !_valid_type(field->type_code) ||
            (field->name_length != 0 && field->name == NULL)) {
                return false;
        }
        fixed_size =
            field->type_code == EXECUTOR_FORMAT_TYPE_POINTER ? 11u : 7u;
        *size = fixed_size + field->name_length;
        return true;
}

bool executor_helper_create_field_descriptor(
    uint8_t* output, size_t capacity, const struct executor_format_field* field,
    size_t* written) {
        size_t required;
        size_t name_offset;

        if (output == NULL || written == NULL ||
            !executor_helper_field_descriptor_size(field, &required) ||
            capacity < required) {
                return false;
        }
        executor_helper_write_u32(output, field->field_id);
        output[4] = field->type_code;
        executor_helper_write_u16(output + 5, field->name_length);
        name_offset = 7;
        if (field->type_code == EXECUTOR_FORMAT_TYPE_POINTER) {
                executor_helper_write_u32(output + 7, field->target_class_id);
                name_offset += 4;
        }
        if (field->name_length != 0) {
                for (size_t i = 0; i < field->name_length; i++) {
                        output[name_offset + i] = field->name[i];
                }
        }
        *written = required;
        return true;
}

bool executor_helper_read_field_descriptor(
    const uint8_t* bytes, size_t length, size_t* offset,
    struct executor_format_field* field) {
        size_t start;
        size_t name_offset;
        size_t descriptor_length;
        uint8_t type_code;
        uint16_t name_length;

        if (bytes == NULL || offset == NULL || field == NULL ||
            *offset > length || length - *offset < 7) {
                return false;
        }
        start     = *offset;
        type_code = bytes[start + 4];
        if (!_valid_type(type_code)) {
                return false;
        }
        name_length = executor_helper_read_u16(bytes + start + 5);
        name_offset = start + 7;
        if (type_code == EXECUTOR_FORMAT_TYPE_POINTER) {
                if (length - start < 11) {
                        return false;
                }
                field->target_class_id =
                    executor_helper_read_u32(bytes + start + 7);
                name_offset += 4;
        } else {
                field->target_class_id = 0;
        }
        descriptor_length = name_offset - start + name_length;
        if (descriptor_length > length - start) {
                return false;
        }
        field->field_id    = executor_helper_read_u32(bytes + start);
        field->type_code   = type_code;
        field->name        = bytes + name_offset;
        field->name_length = name_length;
        *offset += descriptor_length;
        return true;
}
