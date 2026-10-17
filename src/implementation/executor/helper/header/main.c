#include "implementation/executor/helper/header/main.h"

#include <stddef.h>
#include <string.h>

#include "implementation/executor/helper/basic/main.h"
#include "implementation/executor/helper/binary_primitives/main.h"

static const uint8_t helper_database_magic[8] = {'O', 'O',  'P',  'D',
                                                 'B', '\0', '\0', '\0'};

int helper_write_header(FILE* file, const helper_database_header* header) {
        if (file == NULL || header == NULL) {
                return -1;
        }

        if (helper_write_bytes(file, helper_database_magic,
                               sizeof(helper_database_magic)) != 0) {
                return -1;
        }

        if (helper_write_u16(file, header->interface_count) != 0) {
                return -1;
        }

        if (helper_write_u16(file, header->class_count) != 0) {
                return -1;
        }

        if (helper_write_u16(file, header->function_count) != 0) {
                return -1;
        }

        if (helper_write_u16(file, header->reserved) != 0) {
                return -1;
        }

        return 0;
}

int helper_read_header(FILE* file, helper_database_header* header) {
        uint8_t magic[sizeof(helper_database_magic)];
        helper_database_header result;

        if (file == NULL || header == NULL) {
                return -1;
        }

        if (helper_read_bytes(file, magic, sizeof(magic)) != 0) {
                return -1;
        }

        if (memcmp(magic, helper_database_magic, sizeof(magic)) != 0) {
                return -1;
        }

        if (helper_read_u16(file, &result.interface_count) != 0) {
                return -1;
        }

        if (helper_read_u16(file, &result.class_count) != 0) {
                return -1;
        }

        if (helper_read_u16(file, &result.function_count) != 0) {
                return -1;
        }

        if (helper_read_u16(file, &result.reserved) != 0) {
                return -1;
        }

        *header = result;
        return 0;
}
