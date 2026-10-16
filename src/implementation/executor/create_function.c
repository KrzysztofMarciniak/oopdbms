/**
 * @file create_function.c
 * @brief CREATE FUNCTION <class>.<n> (<arg> <type>[, ...]) RETURNS <type> AS
 * <expression>
 */

#include "implementation/executor/executor_internal.h"

/** Not written yet, see executor_internal.h. */
enum EXECUTOR_RESULT execute_create_function(
    struct database* db, const struct analyzer_statement* statement) {
        (void)db;
        (void)statement;
        return EXECUTOR_ERROR_NOT_IMPLEMENTED;
}
