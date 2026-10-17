/**
 * @file binary_encoding.h
 * @brief Little-endian primitives and bounds checks for the database format.
 */

#ifndef IMPLEMENTATION_EXECUTOR_HELPER_BINARY_ENCODING_H
#define IMPLEMENTATION_EXECUTOR_HELPER_BINARY_ENCODING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

uint16_t executor_helper_read_u16(const uint8_t* bytes);
uint32_t executor_helper_read_u32(const uint8_t* bytes);
uint64_t executor_helper_read_u64(const uint8_t* bytes);
void executor_helper_write_u16(uint8_t* bytes, uint16_t value);
void executor_helper_write_u32(uint8_t* bytes, uint32_t value);
void executor_helper_write_u64(uint8_t* bytes, uint64_t value);

bool executor_helper_range_valid(size_t buffer_size, uint64_t offset,
                                 uint64_t length);
bool executor_helper_add_u64(uint64_t left, uint64_t right, uint64_t* result);
bool executor_helper_multiply_u64(uint64_t left, uint64_t right,
                                  uint64_t* result);

#endif
