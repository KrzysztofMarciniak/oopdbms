
#ifndef HELPER_INTERFACE_TABLE_MAIN_H
#define HELPER_INTERFACE_TABLE_MAIN_H

#include <stdint.h>
#include <stdio.h>

#define HELPER_INTERFACE_TABLE_ENTRY_SIZE 16u

typedef struct helper_interface_table_entry {
        uint32_t interface_id;
        uint64_t definition_offset;
        uint32_t definition_length;
} helper_interface_table_entry;

/*
 * Serialize one interface table entry (16 bytes).
 *
 * Layout:
 *   u32 interface_id
 *   u64 definition_offset
 *   u32 definition_length
 *
 * All integers are little-endian.
 *
 * Returns 0 on success, -1 on failure.
 */
int helper_write_interface_table_entry(
    FILE* file, const helper_interface_table_entry* entry);

/*
 * Deserialize one interface table entry (16 bytes).
 *
 * The output structure is assigned only after all fields are read.
 *
 * Returns 0 on success, -1 on failure.
 */
int helper_read_interface_table_entry(FILE* file,
                                      helper_interface_table_entry* entry);

#endif
