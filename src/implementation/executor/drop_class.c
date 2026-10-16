/**
 * @file drop_class.c
 * @brief DROP CLASS <n>
 */

#include "implementation/executor/executor_internal.h"

/** Not written yet, see executor_internal.h. */
enum EXECUTOR_RESULT execute_drop_class(
    struct database* db, const struct analyzer_statement* statement) {
        (void)db;
        (void)statement;
        return EXECUTOR_ERROR_NOT_IMPLEMENTED;
}
