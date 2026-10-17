/**
 * @file create_field_descriptor.h
 * @brief Create and read one variable-length schema field descriptor.
 */

#ifndef IMPLEMENTATION_EXECUTOR_HELPER_CREATE_FIELD_DESCRIPTOR_H
#define IMPLEMENTATION_EXECUTOR_HELPER_CREATE_FIELD_DESCRIPTOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "implementation/executor/helper/format_types.h"

bool executor_helper_field_descriptor_size(
    const struct executor_format_field* field, size_t* size);
bool executor_helper_create_field_descriptor(
    uint8_t* output, size_t capacity, const struct executor_format_field* field,
    size_t* written);
bool executor_helper_read_field_descriptor(const uint8_t* bytes, size_t length,
                                           size_t* offset,
                                           struct executor_format_field* field);

#endif
