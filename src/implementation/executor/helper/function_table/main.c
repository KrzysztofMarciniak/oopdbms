
#include "implementation/executor/helper/function_table/main.h"

#include "implementation/executor/helper/binary_primitives/main.h"

int helper_write_function_table_entry(
    FILE* file, const helper_function_table_entry* entry) {
        if (file == NULL || entry == NULL) {
                return -1;
        }

        if (helper_write_u32(file, entry->function_id) != 0) {
                return -1;
        }

        if (helper_write_u32(file, entry->owner_class_id) != 0) {
                return -1;
        }

        if (helper_write_u64(file, entry->definition_offset) != 0) {
                return -1;
        }

        if (helper_write_u32(file, entry->definition_length) != 0) {
                return -1;
        }

        return 0;
}

int helper_read_function_table_entry(FILE* file,
                                     helper_function_table_entry* entry) {
        helper_function_table_entry result;

        if (file == NULL || entry == NULL) {
                return -1;
        }

        if (helper_read_u32(file, &result.function_id) != 0) {
                return -1;
        }

        if (helper_read_u32(file, &result.owner_class_id) != 0) {
                return -1;
        }

        if (helper_read_u64(file, &result.definition_offset) != 0) {
                return -1;
        }

        if (helper_read_u32(file, &result.definition_length) != 0) {
                return -1;
        }

        *entry = result;
        return 0;
}
