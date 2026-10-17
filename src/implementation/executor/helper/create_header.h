/**
 * @file create_header.h
 * @brief Create and read the fixed 16-byte database header.
 */

#ifndef IMPLEMENTATION_EXECUTOR_HELPER_CREATE_HEADER_H
#define IMPLEMENTATION_EXECUTOR_HELPER_CREATE_HEADER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "implementation/executor/helper/format_types.h"

#define EXECUTOR_FORMAT_HEADER_SIZE 16u

bool executor_helper_create_header(uint8_t* output, size_t capacity,
                                   const struct executor_format_header* header);
bool executor_helper_read_header(const uint8_t* bytes, size_t length,
                                 struct executor_format_header* header);

#endif
