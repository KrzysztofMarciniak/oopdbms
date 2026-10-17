
#ifndef HELPER_METATABLE_MAIN_H
#define HELPER_METATABLE_MAIN_H

#include <stdint.h>
#include <stdio.h>

#define HELPER_METATABLE_ENTRY_COUNT 3u
#define HELPER_METATABLE_ENTRY_SIZE 16u
#define HELPER_METATABLE_SIZE 48u

typedef struct helper_metatable_entry {
        uint64_t offset;
        uint64_t length;
} helper_metatable_entry;

typedef struct helper_metatable {
        helper_metatable_entry interface_table;
        helper_metatable_entry class_table;
        helper_metatable_entry function_table;
} helper_metatable;

/*
 * Serialize the 48-byte database metatable at the current file position.
 *
 * Each entry consists of:
 *   u64 offset (little-endian)
 *   u64 length (little-endian)
 *
 * Entry order:
 *   1. Interface table
 *   2. Class table
 *   3. Function table
 *
 * Returns 0 on success, -1 on failure.
 */
int helper_write_metatable(FILE* file, const helper_metatable* metatable);

/*
 * Deserialize the 48-byte database metatable from the current file position.
 *
 * The output structure is assigned only after all entries are read
 * successfully.
 *
 * Returns 0 on success, -1 on failure.
 */
int helper_read_metatable(FILE* file, helper_metatable* metatable);

#endif
