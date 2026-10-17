/**
 * @file create_function_definition.h
 * @brief Create a function definition with its signature and source expression.
 */

#ifndef IMPLEMENTATION_EXECUTOR_HELPER_CREATE_FUNCTION_DEFINITION_H
#define IMPLEMENTATION_EXECUTOR_HELPER_CREATE_FUNCTION_DEFINITION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "implementation/executor/helper/format_types.h"

bool executor_helper_create_function_definition(
    uint8_t* output, size_t capacity,
    const struct executor_format_function_definition* definition,
    size_t* written);

#endif
