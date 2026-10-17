
#ifndef OOPDBMS_EXECUTOR_HELPER_BINARY_PRIMITIVES_MAIN_H
#define OOPDBMS_EXECUTOR_HELPER_BINARY_PRIMITIVES_MAIN_H

#include <stdint.h>
#include <stdio.h>

/* Unsigned integers: little-endian. */
int helper_write_u8(FILE* file, uint8_t value);
int helper_write_u16(FILE* file, uint16_t value);
int helper_write_u32(FILE* file, uint32_t value);
int helper_write_u64(FILE* file, uint64_t value);

int helper_read_u8(FILE* file, uint8_t* value);
int helper_read_u16(FILE* file, uint16_t* value);
int helper_read_u32(FILE* file, uint32_t* value);
int helper_read_u64(FILE* file, uint64_t* value);

/* Signed 64-bit integer: two's-complement, little-endian. */
int helper_write_i64(FILE* file, int64_t value);
int helper_read_i64(FILE* file, int64_t* value);

/* IEEE 754 binary64: little-endian. */
int helper_write_f64(FILE* file, double value);
int helper_read_f64(FILE* file, double* value);

#endif
