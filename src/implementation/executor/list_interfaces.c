/**
 * @file list_interfaces.c
 * @brief LIST INTERFACES
 */

#include <stdio.h>

#include "implementation/executor/executor_internal.h"
#include "interface/storage/directory.h"

static void _print_interface_name(const struct dir_entry* entry,
                                  void* context) {
        (void)context;
        printf("%.*s\n", (int)entry->name_length, entry->name);
}

enum EXECUTOR_RESULT execute_list_interfaces(
    struct database* db, const struct analyzer_statement* statement) {
        if (db == NULL || statement == NULL) {
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }
        if (db->hdr.iface_count == 0) {
                return EXECUTOR_OK;
        }
        if (!db_dir_visit(db, IFACE_DIR, _print_interface_name, NULL)) {
                return EXECUTOR_ERROR_STORAGE_FAILED;
        }
        return EXECUTOR_OK;
}
