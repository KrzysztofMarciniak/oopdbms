/**
 * @file create_interface_definition.h
 * @brief Create an interface definition and its required field descriptors.
 */

#ifndef IMPLEMENTATION_EXECUTOR_HELPER_CREATE_INTERFACE_DEFINITION_H
#define IMPLEMENTATION_EXECUTOR_HELPER_CREATE_INTERFACE_DEFINITION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "implementation/executor/helper/format_types.h"

bool executor_helper_create_interface_definition(
    uint8_t* output, size_t capacity,
    const struct executor_format_interface_definition* definition,
    size_t* written);

#endif
