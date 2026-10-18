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

size_t executor_helper_varint_size(uint64_t value) {
        size_t length = 1;

        while (value >= UINT64_C(0x80)) {
                value >>= 7;
                length++;
        }
        return length;
}

size_t executor_helper_svarint_size(int64_t value) {
        uint64_t encoded;

        if (value >= 0) {
                encoded = (uint64_t)value << 1;
        } else {
                encoded = ((uint64_t)(-(value + 1)) << 1) | UINT64_C(1);
        }
        return executor_helper_varint_size(encoded);
}

bool executor_helper_write_varint(uint8_t* bytes, size_t capacity,
                                  size_t* offset, uint64_t value) {
        size_t cursor;

        if (bytes == NULL || offset == NULL || *offset > capacity ||
            executor_helper_varint_size(value) > capacity - *offset) {
                return false;
        }
        cursor = *offset;
        while (value >= UINT64_C(0x80)) {
                bytes[cursor++] = (uint8_t)(value & UINT64_C(0x7f)) | 0x80;
                value >>= 7;
        }
        bytes[cursor++] = (uint8_t)value;
        *offset         = cursor;
        return true;
}

bool executor_helper_read_varint(const uint8_t* bytes, size_t length,
                                 size_t* offset, uint64_t* value) {
        uint64_t result = 0;
        size_t cursor;
        size_t index;

        if (bytes == NULL || offset == NULL || value == NULL ||
            *offset > length) {
                return false;
        }
        cursor = *offset;
        for (index = 0; index < 10; index++) {
                uint8_t byte;
                uint8_t payload;

                if (cursor >= length) {
                        return false;
                }
                byte    = bytes[cursor++];
                payload = byte & 0x7f;
                if ((index == 9 && (payload > 1 || (byte & 0x80) != 0)) ||
                    ((byte & 0x80) == 0 && index != 0 && payload == 0)) {
                        return false;
                }
                result |= (uint64_t)payload << (index * 7);
                if ((byte & 0x80) == 0) {
                        *offset = cursor;
                        *value  = result;
                        return true;
                }
        }
        return false;
}

bool executor_helper_write_svarint(uint8_t* bytes, size_t capacity,
                                   size_t* offset, int64_t value) {
        uint64_t encoded;

        if (value >= 0) {
                encoded = (uint64_t)value << 1;
        } else {
                encoded = ((uint64_t)(-(value + 1)) << 1) | UINT64_C(1);
        }
        return executor_helper_write_varint(bytes, capacity, offset, encoded);
}

bool executor_helper_read_svarint(const uint8_t* bytes, size_t length,
                                  size_t* offset, int64_t* value) {
        uint64_t encoded;
        uint64_t magnitude;

        if (value == NULL ||
            !executor_helper_read_varint(bytes, length, offset, &encoded)) {
                return false;
        }
        magnitude = encoded >> 1;
        if ((encoded & 1) == 0) {
                *value = (int64_t)magnitude;
        } else if (magnitude == INT64_MAX) {
                *value = INT64_MIN;
        } else {
                *value = -(int64_t)magnitude - 1;
        }
        return true;
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
