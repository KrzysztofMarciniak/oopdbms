/**
 * @file list_classes.c
 * @brief LIST CLASSES
 */

#include <stdio.h>

#include "implementation/executor/executor_internal.h"
#include "interface/storage/directory.h"

static void _print_class_name(const struct dir_entry* entry, void* context) {
        (void)context;
        printf("%.*s\n", (int)entry->name_length, entry->name);
}

enum EXECUTOR_RESULT execute_list_classes(
    struct database* db, const struct analyzer_statement* statement) {
        if (db == NULL || statement == NULL) {
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }
        if (db->hdr.class_count == 0) {
                return EXECUTOR_OK;
        }
        if (!db_dir_visit(db, CLASS_DIR, _print_class_name, NULL)) {
                return EXECUTOR_ERROR_STORAGE_FAILED;
        }
        return EXECUTOR_OK;
}
