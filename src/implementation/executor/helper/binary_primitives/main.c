
#include "main.h"

#include <limits.h>
#include <string.h>

#include "implementation/executor/helper/basic/main.h"

int helper_write_u8(FILE* file, uint8_t value) {
        return helper_write_bytes(file, &value, 1);
}

int helper_write_u16(FILE* file, uint16_t value) {
        unsigned char bytes[2] = {(unsigned char)value,
                                  (unsigned char)(value >> 8)};

        return helper_write_bytes(file, bytes, sizeof(bytes));
}

int helper_write_u32(FILE* file, uint32_t value) {
        unsigned char bytes[4];
        unsigned int i;

        for (i = 0; i < 4; ++i) bytes[i] = (unsigned char)(value >> (8 * i));

        return helper_write_bytes(file, bytes, sizeof(bytes));
}

int helper_write_u64(FILE* file, uint64_t value) {
        unsigned char bytes[8];
        unsigned int i;

        for (i = 0; i < 8; ++i) bytes[i] = (unsigned char)(value >> (8 * i));

        return helper_write_bytes(file, bytes, sizeof(bytes));
}

int helper_read_u8(FILE* file, uint8_t* value) {
        if (value == NULL) return -1;

        return helper_read_bytes(file, value, 1);
}

int helper_read_u16(FILE* file, uint16_t* value) {
        unsigned char bytes[2];

        if (value == NULL || helper_read_bytes(file, bytes, sizeof(bytes)) != 0)
                return -1;

        *value = (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);

        return 0;
}

int helper_read_u32(FILE* file, uint32_t* value) {
        unsigned char bytes[4];

        if (value == NULL || helper_read_bytes(file, bytes, sizeof(bytes)) != 0)
                return -1;

        *value = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
                 ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);

        return 0;
}

int helper_read_u64(FILE* file, uint64_t* value) {
        unsigned char bytes[8];
        uint64_t result = 0;
        unsigned int i;

        if (value == NULL || helper_read_bytes(file, bytes, sizeof(bytes)) != 0)
                return -1;

        for (i = 0; i < 8; ++i) result |= (uint64_t)bytes[i] << (8 * i);

        *value = result;
        return 0;
}

static uint64_t helper_i64_to_bits(int64_t value) {
        if (value >= 0) return (uint64_t)value;

        return UINT64_MAX - (uint64_t)(-(value + 1));
}

int helper_write_i64(FILE* file, int64_t value) {
        return helper_write_u64(file, helper_i64_to_bits(value));
}

int helper_read_i64(FILE* file, int64_t* value) {
        uint64_t bits;
        uint64_t magnitude;

        if (value == NULL || helper_read_u64(file, &bits) != 0) return -1;

        if (bits <= (uint64_t)INT64_MAX) {
                *value = (int64_t)bits;
                return 0;
        }

        magnitude = UINT64_MAX - bits;

        if (magnitude == (uint64_t)INT64_MAX) {
                *value = INT64_MIN;
                return 0;
        }

        *value = -(int64_t)(magnitude + 1);
        return 0;
}

static int helper_f64_supported(void) {
        return CHAR_BIT == 8 && sizeof(double) == 8 && sizeof(uint64_t) == 8;
}

int helper_write_f64(FILE* file, double value) {
        uint64_t bits;

        if (file == NULL || !helper_f64_supported()) return -1;

        memcpy(&bits, &value, sizeof(bits));

        return helper_write_u64(file, bits);
}

int helper_read_f64(FILE* file, double* value) {
        uint64_t bits;
        double result;

        if (file == NULL || value == NULL || !helper_f64_supported()) return -1;

        if (helper_read_u64(file, &bits) != 0) return -1;

        memcpy(&result, &bits, sizeof(result));
        *value = result;

        return 0;
}
