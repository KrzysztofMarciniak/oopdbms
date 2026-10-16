/**
 * @file create_interface.c
 * @brief CREATE INTERFACE <n> (<field> <type>[, ...])
 */

#include "implementation/executor/executor_internal.h"

/** Not written yet, see executor_internal.h. */
enum EXECUTOR_RESULT execute_create_interface(
    struct database* db, const struct analyzer_statement* statement) {
        (void)db;
        (void)statement;
        return EXECUTOR_ERROR_NOT_IMPLEMENTED;
}
