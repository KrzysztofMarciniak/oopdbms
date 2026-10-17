
#ifndef HELPER_CLASS_TABLE_MAIN_H
#define HELPER_CLASS_TABLE_MAIN_H

#include <stdint.h>
#include <stdio.h>

#define HELPER_CLASS_TABLE_ENTRY_SIZE 48u

typedef struct helper_class_table_entry {
        uint32_t class_id;
        uint64_t definition_offset;
        uint32_t definition_length;
        uint64_t data_offset;
        uint64_t data_length;
        uint64_t object_count;
        uint64_t next_object_id;
} helper_class_table_entry;

/*
 * Serialize one class table entry (48 bytes).
 * All multibyte integers are little-endian.
 *
 * Returns 0 on success, -1 on failure.
 */
int helper_write_class_table_entry(FILE* file,
                                   const helper_class_table_entry* entry);

/*
 * Deserialize one class table entry (48 bytes).
 * The output is assigned only after all fields are read.
 *
 * Returns 0 on success, -1 on failure.
 */
int helper_read_class_table_entry(FILE* file, helper_class_table_entry* entry);

#endif
