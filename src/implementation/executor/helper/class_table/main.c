
#include "implementation/executor/helper/class_table/main.h"

#include "implementation/executor/helper/binary_primitives/main.h"

int helper_write_class_table_entry(FILE* file,
                                   const helper_class_table_entry* entry) {
        if (file == NULL || entry == NULL) {
                return -1;
        }

        if (helper_write_u32(file, entry->class_id) != 0) {
                return -1;
        }

        if (helper_write_u64(file, entry->definition_offset) != 0) {
                return -1;
        }

        if (helper_write_u32(file, entry->definition_length) != 0) {
                return -1;
        }

        if (helper_write_u64(file, entry->data_offset) != 0) {
                return -1;
        }

        if (helper_write_u64(file, entry->data_length) != 0) {
                return -1;
        }

        if (helper_write_u64(file, entry->object_count) != 0) {
                return -1;
        }

        if (helper_write_u64(file, entry->next_object_id) != 0) {
                return -1;
        }

        return 0;
}

int helper_read_class_table_entry(FILE* file, helper_class_table_entry* entry) {
        helper_class_table_entry result;

        if (file == NULL || entry == NULL) {
                return -1;
        }

        if (helper_read_u32(file, &result.class_id) != 0) {
                return -1;
        }

        if (helper_read_u64(file, &result.definition_offset) != 0) {
                return -1;
        }

        if (helper_read_u32(file, &result.definition_length) != 0) {
                return -1;
        }

        if (helper_read_u64(file, &result.data_offset) != 0) {
                return -1;
        }

        if (helper_read_u64(file, &result.data_length) != 0) {
                return -1;
        }

        if (helper_read_u64(file, &result.object_count) != 0) {
                return -1;
        }

        if (helper_read_u64(file, &result.next_object_id) != 0) {
                return -1;
        }

        *entry = result;
        return 0;
}
