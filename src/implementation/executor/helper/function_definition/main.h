
#ifndef HELPER_FUNCTION_DEFINITION_MAIN_H
#define HELPER_FUNCTION_DEFINITION_MAIN_H

#include <stdint.h>
#include <stdio.h>

#define HELPER_FUNCTION_TYPE_INTEGER 1u
#define HELPER_FUNCTION_TYPE_FLOAT 2u
#define HELPER_FUNCTION_TYPE_TEXT 3u
#define HELPER_FUNCTION_TYPE_BOOL 4u
#define HELPER_FUNCTION_TYPE_POINTER 5u

typedef struct helper_function_type_descriptor {
        uint8_t type_code;
        uint32_t target_class_id;
} helper_function_type_descriptor;

typedef struct helper_function_parameter {
        char* name;
        helper_function_type_descriptor type;
} helper_function_parameter;

typedef struct helper_function_definition {
        char* name;
        uint16_t parameter_count;
        helper_function_type_descriptor return_type;
        uint32_t expression_length;
        helper_function_parameter* parameters;
        uint8_t* expression;
} helper_function_definition;

/*
 * Serialize a function definition at the current file position.
 *
 * Format:
 *   u16 name_length
 *   u16 parameter_count
 *   return type descriptor
 *   u32 expression_length
 *   name bytes
 *   parameter descriptors
 *   expression bytes
 *
 * A type descriptor consists of:
 *   u8 type_code
 *   u32 target_class_id, only when type_code is POINTER
 *
 * Each parameter descriptor consists of:
 *   u16 name_length
 *   type descriptor
 *   parameter name bytes
 *
 * All multibyte integers are little-endian.
 *
 * Returns 0 on success, -1 on failure.
 */
int helper_write_function_definition(
    FILE* file, const helper_function_definition* definition);

/*
 * Deserialize a function definition from the current file position.
 *
 * Allocates the function name, parameter array, parameter names, and
 * expression bytes. The caller owns these allocations and must release
 * them using helper_free_function_definition().
 *
 * The output structure is assigned only after successful deserialization.
 *
 * Returns 0 on success, -1 on failure.
 */
int helper_read_function_definition(FILE* file,
                                    helper_function_definition* definition);

/* Release memory owned by a deserialized function definition. */
void helper_free_function_definition(helper_function_definition* definition);

#endif
