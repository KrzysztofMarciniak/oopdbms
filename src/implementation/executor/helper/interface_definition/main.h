
#ifndef HELPER_INTERFACE_DEFINITION_MAIN_H
#define HELPER_INTERFACE_DEFINITION_MAIN_H

#include <stdint.h>
#include <stdio.h>

#define HELPER_INTERFACE_DEFINITION_HEADER_SIZE 4u
#define HELPER_FIELD_DESCRIPTOR_FIXED_SIZE 7u

#define HELPER_TYPE_INTEGER 1u
#define HELPER_TYPE_FLOAT 2u
#define HELPER_TYPE_TEXT 3u
#define HELPER_TYPE_BOOL 4u
#define HELPER_TYPE_POINTER 5u

typedef struct helper_interface_field {
        uint32_t field_id;
        uint8_t type_code;
        uint32_t target_class_id;
        char* name;
} helper_interface_field;

typedef struct helper_interface_definition {
        char* name;
        uint16_t field_count;
        helper_interface_field* fields;
} helper_interface_definition;

/*
 * Serialize an interface definition at the current file position.
 *
 * Layout:
 *   u16 name_length
 *   u16 field_count
 *   name bytes
 *   field descriptors
 *
 * Each field descriptor:
 *   u32 field_id
 *   u8  type_code
 *   u16 name_length
 *   [u32 target_class_id, if type_code is HELPER_TYPE_POINTER]
 *   field name bytes
 *
 * Returns 0 on success, -1 on failure.
 */
int helper_write_interface_definition(
    FILE* file, const helper_interface_definition* definition);

/*
 * Deserialize an interface definition from the current file position.
 *
 * Allocates the definition name, field array, and field names.
 * On success, the caller owns the allocated memory and must release it
 * with helper_free_interface_definition().
 *
 * Returns 0 on success, -1 on failure.
 */
int helper_read_interface_definition(FILE* file,
                                     helper_interface_definition* definition);

/* Release memory owned by a deserialized interface definition. */
void helper_free_interface_definition(helper_interface_definition* definition);

#endif
