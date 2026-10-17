/**
 * @file binary_encoding.c
 * @brief Little-endian primitives and bounds checks for the database format.
 */

#include "implementation/executor/helper/binary_encoding.h"

#include <limits.h>

uint16_t executor_helper_read_u16(const uint8_t* bytes) {
        return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

uint32_t executor_helper_read_u32(const uint8_t* bytes) {
        return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
               ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

uint64_t executor_helper_read_u64(const uint8_t* bytes) {
        return (uint64_t)executor_helper_read_u32(bytes) |
               ((uint64_t)executor_helper_read_u32(bytes + 4) << 32);
}

void executor_helper_write_u16(uint8_t* bytes, uint16_t value) {
        bytes[0] = (uint8_t)value;
        bytes[1] = (uint8_t)(value >> 8);
}

void executor_helper_write_u32(uint8_t* bytes, uint32_t value) {
        bytes[0] = (uint8_t)value;
        bytes[1] = (uint8_t)(value >> 8);
        bytes[2] = (uint8_t)(value >> 16);
        bytes[3] = (uint8_t)(value >> 24);
}

void executor_helper_write_u64(uint8_t* bytes, uint64_t value) {
        executor_helper_write_u32(bytes, (uint32_t)value);
        executor_helper_write_u32(bytes + 4, (uint32_t)(value >> 32));
}

bool executor_helper_range_valid(size_t buffer_size, uint64_t offset,
                                 uint64_t length) {
        return offset <= (uint64_t)buffer_size &&
               length <= (uint64_t)buffer_size - offset;
}

bool executor_helper_add_u64(uint64_t left, uint64_t right, uint64_t* result) {
        if (result == NULL || right > UINT64_MAX - left) {
                return false;
        }
        *result = left + right;
        return true;
}

bool executor_helper_multiply_u64(uint64_t left, uint64_t right,
                                  uint64_t* result) {
        if (result == NULL || (left != 0 && right > UINT64_MAX / left)) {
                return false;
        }
        *result = left * right;
        return true;
}
