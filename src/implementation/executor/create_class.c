/**
 * @file create_class.c
 * @brief CREATE CLASS <n> [EXTENDS <class>] [IMPLEMENTS <interface>[, ...]]
 * (<field> <type>[, ...])
 */

#include "implementation/executor/executor_internal.h"

/** Not written yet, see executor_internal.h. */
enum EXECUTOR_RESULT execute_create_class(
    struct database* db, const struct analyzer_statement* statement) {
        (void)statement;
        database_mark_modified(db);
        return EXECUTOR_OK;
}
