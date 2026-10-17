/**
 * @file create_header.c
 * @brief Create and read the fixed 16-byte database header.
 */

#include "implementation/executor/helper/create_header.h"

#include <string.h>

#include "implementation/executor/helper/binary_encoding.h"

static const uint8_t MAGIC[8] = {'O', 'O', 'P', 'D', 'B', 0, 0, 0};

bool executor_helper_create_header(
    uint8_t* output, size_t capacity,
    const struct executor_format_header* header) {
        if (output == NULL || header == NULL ||
            capacity < EXECUTOR_FORMAT_HEADER_SIZE) {
                return false;
        }
        memcpy(output, MAGIC, sizeof MAGIC);
        executor_helper_write_u16(output + 8, header->interface_count);
        executor_helper_write_u16(output + 10, header->class_count);
        executor_helper_write_u16(output + 12, header->function_count);
        executor_helper_write_u16(output + 14, 0);
        return true;
}

bool executor_helper_read_header(const uint8_t* bytes, size_t length,
                                 struct executor_format_header* header) {
        if (bytes == NULL || header == NULL ||
            length < EXECUTOR_FORMAT_HEADER_SIZE ||
            memcmp(bytes, MAGIC, sizeof MAGIC) != 0 ||
            executor_helper_read_u16(bytes + 14) != 0) {
                return false;
        }
        header->interface_count = executor_helper_read_u16(bytes + 8);
        header->class_count     = executor_helper_read_u16(bytes + 10);
        header->function_count  = executor_helper_read_u16(bytes + 12);
        return true;
}
