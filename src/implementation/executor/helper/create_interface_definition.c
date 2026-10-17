/**
 * @file create_interface_definition.c
 * @brief Create an interface definition and its required field descriptors.
 */

#include "implementation/executor/helper/create_interface_definition.h"

#include "implementation/executor/helper/binary_encoding.h"
#include "implementation/executor/helper/create_field_descriptor.h"

bool executor_helper_create_interface_definition(
    uint8_t* output, size_t capacity,
    const struct executor_format_interface_definition* definition,
    size_t* written) {
        uint64_t required = 4;
        size_t offset     = 4;
        size_t i;

        if (output == NULL || definition == NULL || written == NULL ||
            (definition->name_length != 0 && definition->name == NULL) ||
            (definition->field_count != 0 && definition->fields == NULL)) {
                return false;
        }
        for (i = 0; i < definition->field_count; i++) {
                size_t field_size;

                uint64_t next_size;

                if (!executor_helper_field_descriptor_size(
                        &definition->fields[i], &field_size) ||
                    !executor_helper_add_u64(required, field_size,
                                             &next_size)) {
                        return false;
                }
                required = next_size;
        }
        {
                uint64_t next_size;

                if (!executor_helper_add_u64(required, definition->name_length,
                                             &next_size)) {
                        return false;
                }
                required = next_size;
        }
        if (required > UINT32_MAX || capacity < required) {
                return false;
        }
        executor_helper_write_u16(output, definition->name_length);
        executor_helper_write_u16(output + 2, definition->field_count);
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
        *written = offset;
        return true;
}
