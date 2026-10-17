/**
 * @file create_class_definition.c
 * @brief Create a class definition and its directly declared fields.
 */

#include "implementation/executor/helper/create_class_definition.h"

#include "implementation/executor/helper/binary_encoding.h"
#include "implementation/executor/helper/create_field_descriptor.h"

bool executor_helper_create_class_definition(
    uint8_t* output, size_t capacity,
    const struct executor_format_class_definition* definition,
    size_t* written) {
        uint64_t required = 16;
        uint64_t next_size;
        size_t offset = 16;
        size_t i;

        if (output == NULL || definition == NULL || written == NULL ||
            (definition->name_length != 0 && definition->name == NULL) ||
            (definition->field_count != 0 && definition->fields == NULL) ||
            (definition->interface_count != 0 &&
             definition->interface_ids == NULL) ||
            !executor_helper_add_u64(required, definition->name_length,
                                     &next_size)) {
                return false;
        }
        required = next_size;
        for (i = 0; i < definition->field_count; i++) {
                size_t field_size;

                if (!executor_helper_field_descriptor_size(
                        &definition->fields[i], &field_size) ||
                    !executor_helper_add_u64(required, field_size,
                                             &next_size)) {
                        return false;
                }
                required = next_size;
        }
        if (!executor_helper_multiply_u64(definition->interface_count, 4,
                                          &next_size) ||
            !executor_helper_add_u64(required, next_size, &required) ||
            required > UINT32_MAX || capacity < required) {
                return false;
        }

        executor_helper_write_u16(output, definition->name_length);
        executor_helper_write_u16(output + 2, definition->flags);
        executor_helper_write_u32(output + 4, definition->parent_class_id);
        executor_helper_write_u32(output + 8, definition->field_count);
        executor_helper_write_u32(output + 12, definition->interface_count);
        if (definition->name_length != 0) {
                for (i = 0; i < definition->name_length; i++) {
                        output[offset + i] = definition->name[i];
                }
        }
        offset += definition->name_length;
        for (i = 0; i < definition->field_count; i++) {
                size_t field_written;

                if (!executor_helper_create_field_descriptor(
                        output + offset, capacity - offset,
                        &definition->fields[i], &field_written)) {
                        return false;
                }
                offset += field_written;
        }
        for (i = 0; i < definition->interface_count; i++) {
                executor_helper_write_u32(output + offset,
                                          definition->interface_ids[i]);
                offset += 4;
        }
        *written = offset;
        return true;
}
