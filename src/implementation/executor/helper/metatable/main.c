
#include "implementation/executor/helper/metatable/main.h"

#include "implementation/executor/helper/binary_primitives/main.h"

int helper_write_metatable(FILE* file, const helper_metatable* metatable) {
        if (file == NULL || metatable == NULL) {
                return -1;
        }

        if (helper_write_u64(file, metatable->interface_table.offset) != 0) {
                return -1;
        }

        if (helper_write_u64(file, metatable->interface_table.length) != 0) {
                return -1;
        }

        if (helper_write_u64(file, metatable->class_table.offset) != 0) {
                return -1;
        }

        if (helper_write_u64(file, metatable->class_table.length) != 0) {
                return -1;
        }

        if (helper_write_u64(file, metatable->function_table.offset) != 0) {
                return -1;
        }

        if (helper_write_u64(file, metatable->function_table.length) != 0) {
                return -1;
        }

        return 0;
}

int helper_read_metatable(FILE* file, helper_metatable* metatable) {
        helper_metatable result;

        if (file == NULL || metatable == NULL) {
                return -1;
        }

        if (helper_read_u64(file, &result.interface_table.offset) != 0) {
                return -1;
        }

        if (helper_read_u64(file, &result.interface_table.length) != 0) {
                return -1;
        }

        if (helper_read_u64(file, &result.class_table.offset) != 0) {
                return -1;
        }

        if (helper_read_u64(file, &result.class_table.length) != 0) {
                return -1;
        }

        if (helper_read_u64(file, &result.function_table.offset) != 0) {
                return -1;
        }

        if (helper_read_u64(file, &result.function_table.length) != 0) {
                return -1;
        }

        *metatable = result;
        return 0;
}
