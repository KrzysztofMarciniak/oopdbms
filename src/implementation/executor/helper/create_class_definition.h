/**
 * @file create_class_definition.h
 * @brief Create a class definition and its directly declared fields.
 */

#ifndef IMPLEMENTATION_EXECUTOR_HELPER_CREATE_CLASS_DEFINITION_H
#define IMPLEMENTATION_EXECUTOR_HELPER_CREATE_CLASS_DEFINITION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "implementation/executor/helper/format_types.h"

bool executor_helper_create_class_definition(
    uint8_t* output, size_t capacity,
    const struct executor_format_class_definition* definition, size_t* written);

#endif
