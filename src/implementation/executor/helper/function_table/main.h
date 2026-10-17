
#ifndef HELPER_FUNCTION_TABLE_MAIN_H
#define HELPER_FUNCTION_TABLE_MAIN_H

#include <stdint.h>
#include <stdio.h>

#define HELPER_FUNCTION_TABLE_ENTRY_SIZE 20u

typedef struct helper_function_table_entry {
        uint32_t function_id;
        uint32_t owner_class_id;
        uint64_t definition_offset;
        uint32_t definition_length;
} helper_function_table_entry;

/*
 * Serialize one function table entry (20 bytes).
 * All multibyte integers are little-endian.
 *
 * Returns 0 on success, -1 on failure.
 */
int helper_write_function_table_entry(FILE* file,
                                      const helper_function_table_entry* entry);

/*
 * Deserialize one function table entry (20 bytes).
 * The output is assigned only after all fields are read.
 *
 * Returns 0 on success, -1 on failure.
 */
int helper_read_function_table_entry(FILE* file,
                                     helper_function_table_entry* entry);

#endif
