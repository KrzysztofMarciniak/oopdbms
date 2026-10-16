/**
 * @file add_field.c
 * @brief ALTER CLASS <n> ADD FIELD <field> <type>
 */

#include "implementation/executor/executor_internal.h"

/** Not written yet, see executor_internal.h. */
enum EXECUTOR_RESULT execute_alter_class_add_field(
    struct database* db, const struct analyzer_statement* statement) {
        (void)db;
        (void)statement;
        return EXECUTOR_ERROR_NOT_IMPLEMENTED;
}
