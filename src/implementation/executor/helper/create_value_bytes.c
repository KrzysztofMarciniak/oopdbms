/**
 * @file create_value_bytes.c
 * @brief Encode scalar values using the type encodings in README.md.
 */

#include "implementation/executor/helper/create_value_bytes.h"

#include <float.h>
#include <string.h>

#include "implementation/executor/helper/binary_encoding.h"

bool executor_helper_create_integer_value(uint8_t* output, size_t capacity,
                                          int64_t value, size_t* written) {
        if (output == NULL || written == NULL || capacity < 8) {
                return false;
        }
        executor_helper_write_u64(output, (uint64_t)value);
        *written = 8;
        return true;
}

bool executor_helper_create_float_value(uint8_t* output, size_t capacity,
                                        double value, size_t* written) {
        static const uint8_t binary64_one_little_endian[8] = {
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x3f};
        static const uint8_t binary64_one_big_endian[8] = {
            0x3f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        uint8_t native_bytes[8];
        double one = 1.0;
        size_t i;

        if (output == NULL || written == NULL || capacity < 8 ||
            sizeof(double) != 8 || FLT_RADIX != 2 || DBL_MANT_DIG != 53 ||
            DBL_MAX_EXP != 1024 || DBL_MIN_EXP != -1021) {
                return false;
        }
        memcpy(native_bytes, &one, sizeof native_bytes);
        if (memcmp(native_bytes, binary64_one_little_endian, 8) == 0) {
                memcpy(native_bytes, &value, sizeof native_bytes);
        } else if (memcmp(native_bytes, binary64_one_big_endian, 8) == 0) {
                memcpy(native_bytes, &value, sizeof native_bytes);
                for (i = 0; i < 8; i++) {
                        output[i] = native_bytes[7 - i];
                }
                *written = 8;
                return true;
        } else {
                return false;
        }
        memcpy(output, native_bytes, sizeof native_bytes);
        *written = 8;
        return true;
}

bool executor_helper_create_text_value(uint8_t* output, size_t capacity,
                                       const uint8_t* value, size_t length,
                                       size_t* written) {
        size_t i;

        if (output == NULL || written == NULL || length > UINT32_MAX ||
            length > capacity || (length != 0 && value == NULL)) {
                return false;
        }
        for (i = 0; i < length; i++) {
                output[i] = value[i];
        }
        *written = length;
        return true;
}

bool executor_helper_create_bool_value(uint8_t* output, size_t capacity,
                                       bool value, size_t* written) {
        if (output == NULL || written == NULL || capacity < 1) {
                return false;
        }
        output[0] = value ? 1 : 0;
        *written  = 1;
        return true;
}

bool executor_helper_create_pointer_value(uint8_t* output, size_t capacity,
                                          uint32_t target_class_id,
                                          uint64_t target_object_id,
                                          size_t* written) {
        if (output == NULL || written == NULL || capacity < 12) {
                return false;
        }
        executor_helper_write_u32(output, target_class_id);
        executor_helper_write_u64(output + 4, target_object_id);
        *written = 12;
        return true;
}
