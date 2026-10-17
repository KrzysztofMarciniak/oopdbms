/**
 * @file create_metatable.c
 * @brief Create and read the 48-byte metadata locator after the file header.
 */

#include "implementation/executor/helper/create_metatable.h"

#include "implementation/executor/helper/binary_encoding.h"

static void _write_section(uint8_t* output,
                           const struct executor_format_section* section) {
        executor_helper_write_u64(output, section->offset);
        executor_helper_write_u64(output + 8, section->length);
}

static void _read_section(const uint8_t* bytes,
                          struct executor_format_section* section) {
        section->offset = executor_helper_read_u64(bytes);
        section->length = executor_helper_read_u64(bytes + 8);
}

bool executor_helper_create_metatable(
    uint8_t* output, size_t capacity,
    const struct executor_format_metatable* metatable) {
        if (output == NULL || metatable == NULL ||
            capacity < EXECUTOR_FORMAT_METATABLE_SIZE) {
                return false;
        }
        _write_section(output, &metatable->interfaces);
        _write_section(output + 16, &metatable->classes);
        _write_section(output + 32, &metatable->functions);
        return true;
}

bool executor_helper_read_metatable(
    const uint8_t* bytes, size_t length,
    struct executor_format_metatable* metatable) {
        if (bytes == NULL || metatable == NULL ||
            length < EXECUTOR_FORMAT_METATABLE_SIZE) {
                return false;
        }
        _read_section(bytes, &metatable->interfaces);
        _read_section(bytes + 16, &metatable->classes);
        _read_section(bytes + 32, &metatable->functions);
        return true;
}
