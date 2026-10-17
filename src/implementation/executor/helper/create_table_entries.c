/**
 * @file create_table_entries.c
 * @brief Create and read interface, class, and function table entries.
 */

#include "implementation/executor/helper/create_table_entries.h"

#include "implementation/executor/helper/binary_encoding.h"

bool executor_helper_create_interface_entry(
    uint8_t* output, size_t capacity,
    const struct executor_format_interface_entry* entry) {
        if (output == NULL || entry == NULL ||
            capacity < EXECUTOR_FORMAT_INTERFACE_ENTRY_SIZE) {
                return false;
        }
        executor_helper_write_u32(output, entry->interface_id);
        executor_helper_write_u64(output + 4, entry->definition_offset);
        executor_helper_write_u32(output + 12, entry->definition_length);
        return true;
}

bool executor_helper_read_interface_entry(
    const uint8_t* bytes, size_t length,
    struct executor_format_interface_entry* entry) {
        if (bytes == NULL || entry == NULL ||
            length < EXECUTOR_FORMAT_INTERFACE_ENTRY_SIZE) {
                return false;
        }
        entry->interface_id      = executor_helper_read_u32(bytes);
        entry->definition_offset = executor_helper_read_u64(bytes + 4);
        entry->definition_length = executor_helper_read_u32(bytes + 12);
        return true;
}

bool executor_helper_create_class_entry(
    uint8_t* output, size_t capacity,
    const struct executor_format_class_entry* entry) {
        if (output == NULL || entry == NULL ||
            capacity < EXECUTOR_FORMAT_CLASS_ENTRY_SIZE) {
                return false;
        }
        executor_helper_write_u32(output, entry->class_id);
        executor_helper_write_u64(output + 4, entry->definition_offset);
        executor_helper_write_u32(output + 12, entry->definition_length);
        executor_helper_write_u64(output + 16, entry->data_offset);
        executor_helper_write_u64(output + 24, entry->data_length);
        executor_helper_write_u64(output + 32, entry->object_count);
        executor_helper_write_u64(output + 40, entry->next_object_id);
        return true;
}

bool executor_helper_read_class_entry(
    const uint8_t* bytes, size_t length,
    struct executor_format_class_entry* entry) {
        if (bytes == NULL || entry == NULL ||
            length < EXECUTOR_FORMAT_CLASS_ENTRY_SIZE) {
                return false;
        }
        entry->class_id          = executor_helper_read_u32(bytes);
        entry->definition_offset = executor_helper_read_u64(bytes + 4);
        entry->definition_length = executor_helper_read_u32(bytes + 12);
        entry->data_offset       = executor_helper_read_u64(bytes + 16);
        entry->data_length       = executor_helper_read_u64(bytes + 24);
        entry->object_count      = executor_helper_read_u64(bytes + 32);
        entry->next_object_id    = executor_helper_read_u64(bytes + 40);
        return true;
}

bool executor_helper_create_function_entry(
    uint8_t* output, size_t capacity,
    const struct executor_format_function_entry* entry) {
        if (output == NULL || entry == NULL ||
            capacity < EXECUTOR_FORMAT_FUNCTION_ENTRY_SIZE) {
                return false;
        }
        executor_helper_write_u32(output, entry->function_id);
        executor_helper_write_u32(output + 4, entry->owner_class_id);
        executor_helper_write_u64(output + 8, entry->definition_offset);
        executor_helper_write_u32(output + 16, entry->definition_length);
        return true;
}

bool executor_helper_read_function_entry(
    const uint8_t* bytes, size_t length,
    struct executor_format_function_entry* entry) {
        if (bytes == NULL || entry == NULL ||
            length < EXECUTOR_FORMAT_FUNCTION_ENTRY_SIZE) {
                return false;
        }
        entry->function_id       = executor_helper_read_u32(bytes);
        entry->owner_class_id    = executor_helper_read_u32(bytes + 4);
        entry->definition_offset = executor_helper_read_u64(bytes + 8);
        entry->definition_length = executor_helper_read_u32(bytes + 16);
        return true;
}
