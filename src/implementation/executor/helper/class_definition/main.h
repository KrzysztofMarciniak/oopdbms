#ifndef HELPER_CLASS_DEFINITION_MAIN_H
#define HELPER_CLASS_DEFINITION_MAIN_H

#include <stdint.h>
#include <stdio.h>

#define HELPER_CLASS_DEFINITION_HEADER_SIZE 16u

#define HELPER_CLASS_TYPE_INTEGER 1u
#define HELPER_CLASS_TYPE_FLOAT 2u
#define HELPER_CLASS_TYPE_TEXT 3u
#define HELPER_CLASS_TYPE_BOOL 4u
#define HELPER_CLASS_TYPE_POINTER 5u

typedef struct helper_class_field {
        uint32_t field_id;
        uint8_t type_code;
        uint32_t target_class_id;
        char* name;
} helper_class_field;

typedef struct helper_class_definition {
        char* name;
        uint16_t flags;
        uint32_t parent_class_id;
        uint32_t field_count;
        uint32_t interface_count;
        helper_class_field* fields;
        uint32_t* interface_ids;
} helper_class_definition;

/*
 * Serialize a class definition at the current file position.
 *
 * Format:
 *   u16 name_length
 *   u16 flags
 *   u32 parent_class_id
 *   u32 field_count
 *   u32 interface_count
 *   name bytes
 *   field descriptors
 *   interface IDs (u32 each)
 *
 * All multibyte integers are little-endian.
 *
 * Returns 0 on success, -1 on failure.
 */
int helper_write_class_definition(FILE* file,
                                  const helper_class_definition* definition);

/*
 * Deserialize a class definition from the current file position.
 *
 * Allocates the class name, field array, field names, and interface ID
 * array. The caller owns these allocations and must release them using
 * helper_free_class_definition().
 *
 * The output structure is assigned only after successful deserialization.
 *
 * Returns 0 on success, -1 on failure.
 */
int helper_read_class_definition(FILE* file,
                                 helper_class_definition* definition);

/* Release memory owned by a class definition. */
void helper_free_class_definition(helper_class_definition* definition);

#endif
