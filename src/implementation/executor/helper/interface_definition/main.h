
#ifndef HELPER_INTERFACE_DEFINITION_MAIN_H
#define HELPER_INTERFACE_DEFINITION_MAIN_H

#include <stdint.h>
#include <stdio.h>

#include "error/executor/executor_error.h"
#include "interface/analyzer/statement.h"
#include "interface/storage/database.h"

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

typedef struct helper_interface_page_field {
        const char* type_name;
        const char* name;
} helper_interface_page_field;

typedef struct helper_interface_page_definition {
        uint16_t field_count;
        const helper_interface_page_field* fields;
} helper_interface_page_definition;

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
 * Serialize the page-based v2 interface definition.
 *
 * Layout:
 *   u16 field_count
 *   repeated: u16 type_length, type bytes, u16 name_length, name bytes
 *
 * The interface name belongs to the page directory entry, not this blob.
 * Returns 0 on success, -1 on failure.
 */
int helper_write_interface_page_definition(
    uint8_t* buffer, size_t buffer_size,
    const helper_interface_page_definition* definition, uint16_t* written);

/* Validate, serialize, and register a CREATE INTERFACE statement. */
enum EXECUTOR_RESULT helper_create_interface(
    struct database* db, const struct analyzer_statement* statement);

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
