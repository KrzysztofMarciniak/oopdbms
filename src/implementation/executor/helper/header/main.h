#ifndef HELPER_HEADER_MAIN_H
#define HELPER_HEADER_MAIN_H

#include <stdint.h>
#include <stdio.h>

#define HELPER_HEADER_SIZE 16u

typedef struct helper_database_header {
        uint16_t interface_count;
        uint16_t class_count;
        uint16_t function_count;
        uint16_t reserved;
} helper_database_header;

/*
 * Write the database header at the current file position.
 *
 * Format:
 *   8 bytes  magic: "OOPDB\0\0\0"
 *   2 bytes  interface_count (little-endian)
 *   2 bytes  class_count (little-endian)
 *   2 bytes  function_count (little-endian)
 *   2 bytes  reserved (little-endian)
 *
 * Returns 0 on success, -1 on failure.
 */
int helper_write_header(FILE* file, const helper_database_header* header);

/*
 * Read and validate the database magic, then read the header fields.
 *
 * Returns 0 on success, -1 on failure.
 * The output header is only assigned after all fields are read
 * successfully and the magic is valid.
 */
int helper_read_header(FILE* file, helper_database_header* header);

#endif
