
#ifndef OOPDBMS_EXECUTOR_HELPER_BASIC_MAIN_H
#define OOPDBMS_EXECUTOR_HELPER_BASIC_MAIN_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/*
 * Write exactly length bytes to a file.
 *
 * Returns:
 *   0  on success
 *  -1  on failure
 */
int helper_write_bytes(FILE* file, const void* data, size_t length);

/*
 * Read exactly length bytes from a file.
 *
 * Returns:
 *   0  on success
 *  -1  on failure
 */
int helper_read_bytes(FILE* file, void* data, size_t length);

#endif
