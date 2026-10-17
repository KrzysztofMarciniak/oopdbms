/**
 * @file validate_metadata_layout.h
 * @brief Validate metadata table sizes, file ranges, and section overlap.
 */

#ifndef IMPLEMENTATION_EXECUTOR_HELPER_VALIDATE_METADATA_LAYOUT_H
#define IMPLEMENTATION_EXECUTOR_HELPER_VALIDATE_METADATA_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>

#include "implementation/executor/helper/format_types.h"

bool executor_helper_validate_metadata_layout(
    size_t file_size, const struct executor_format_header* header,
    const struct executor_format_metatable* metatable);

#endif
