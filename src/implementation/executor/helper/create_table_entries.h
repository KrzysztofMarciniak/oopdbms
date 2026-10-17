/**
 * @file create_table_entries.h
 * @brief Create and read interface, class, and function table entries.
 */

#ifndef IMPLEMENTATION_EXECUTOR_HELPER_CREATE_TABLE_ENTRIES_H
#define IMPLEMENTATION_EXECUTOR_HELPER_CREATE_TABLE_ENTRIES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "implementation/executor/helper/format_types.h"

#define EXECUTOR_FORMAT_INTERFACE_ENTRY_SIZE 16u
#define EXECUTOR_FORMAT_CLASS_ENTRY_SIZE 48u
#define EXECUTOR_FORMAT_FUNCTION_ENTRY_SIZE 20u

bool executor_helper_create_interface_entry(
    uint8_t* output, size_t capacity,
    const struct executor_format_interface_entry* entry);
bool executor_helper_read_interface_entry(
    const uint8_t* bytes, size_t length,
    struct executor_format_interface_entry* entry);

bool executor_helper_create_class_entry(
    uint8_t* output, size_t capacity,
    const struct executor_format_class_entry* entry);
bool executor_helper_read_class_entry(
    const uint8_t* bytes, size_t length,
    struct executor_format_class_entry* entry);

bool executor_helper_create_function_entry(
    uint8_t* output, size_t capacity,
    const struct executor_format_function_entry* entry);
bool executor_helper_read_function_entry(
    const uint8_t* bytes, size_t length,
    struct executor_format_function_entry* entry);

#endif
