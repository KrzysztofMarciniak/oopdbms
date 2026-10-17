
#ifndef HELPER_OBJECT_RECORD_MAIN_H
#define HELPER_OBJECT_RECORD_MAIN_H

#include <stdint.h>
#include <stdio.h>

#define HELPER_OBJECT_RECORD_HEADER_SIZE 16u
#define HELPER_OBJECT_FIELD_HEADER_SIZE 9u
#define HELPER_OBJECT_POINTER_VALUE_SIZE 12u

#define HELPER_OBJECT_TYPE_INTEGER 1u
#define HELPER_OBJECT_TYPE_FLOAT 2u
#define HELPER_OBJECT_TYPE_TEXT 3u
#define HELPER_OBJECT_TYPE_BOOL 4u
#define HELPER_OBJECT_TYPE_POINTER 5u

typedef struct helper_object_text {
        uint32_t length;
        uint8_t* bytes;
} helper_object_text;

typedef struct helper_object_pointer {
        uint32_t class_id;
        uint64_t object_id;
} helper_object_pointer;

typedef struct helper_object_field {
        uint32_t field_id;
        uint8_t type_code;

        union {
                int64_t integer;
                double floating;
                helper_object_text text;
                uint8_t boolean;
                helper_object_pointer pointer;
        } value;
} helper_object_field;

typedef struct helper_object_record {
        uint64_t object_id;
        uint16_t flags;
        uint16_t field_count;
        helper_object_field* fields;
} helper_object_record;

/*
 * Serialize an object record at the current file position.
 *
 * The serialized record length is calculated from its header, field
 * headers, and field values. It includes the complete 16-byte record
 * header.
 *
 * Returns 0 on success, -1 on failure.
 */
int helper_write_object_record(FILE* file, const helper_object_record* record);

/*
 * Deserialize an object record from the current file position.
 *
 * Allocates the field array and text field bytes as needed.
 * The caller owns these allocations and must release them using
 * helper_free_object_record().
 *
 * The output structure is assigned only after successful deserialization.
 *
 * Returns 0 on success, -1 on failure.
 */
int helper_read_object_record(FILE* file, helper_object_record* record);

/* Release memory owned by an object record. */
void helper_free_object_record(helper_object_record* record);

#endif
