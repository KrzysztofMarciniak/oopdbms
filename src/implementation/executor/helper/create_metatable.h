/**
 * @file create_metatable.h
 * @brief Create and read the 48-byte metadata locator after the file header.
 */

#ifndef IMPLEMENTATION_EXECUTOR_HELPER_CREATE_METATABLE_H
#define IMPLEMENTATION_EXECUTOR_HELPER_CREATE_METATABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "implementation/executor/helper/format_types.h"

#define EXECUTOR_FORMAT_METATABLE_SIZE 48u

bool executor_helper_create_metatable(
    uint8_t* output, size_t capacity,
    const struct executor_format_metatable* metatable);
bool executor_helper_read_metatable(
    const uint8_t* bytes, size_t length,
    struct executor_format_metatable* metatable);

#endif
