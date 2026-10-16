/**
 * @file extends.c
 * @brief ALTER CLASS <n> EXTENDS <class>
 */

#include "implementation/executor/executor_internal.h"

/** Not written yet, see executor_internal.h. */
enum EXECUTOR_RESULT execute_alter_class_extends(
    struct database* db, const struct analyzer_statement* statement) {
        (void)db;
        (void)statement;
        return EXECUTOR_ERROR_NOT_IMPLEMENTED;
}
