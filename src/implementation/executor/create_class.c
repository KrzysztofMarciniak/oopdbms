/**
 * @file create_class.c
 * @brief CREATE CLASS <n> [EXTENDS <class>] [IMPLEMENTS <interface>[, ...]]
 * (<field> <type>[, ...])
 */

#include "implementation/executor/executor_internal.h"
#include "implementation/executor/helper/class_definition/main.h"

enum EXECUTOR_RESULT execute_create_class(
    struct database* db, const struct analyzer_statement* statement) {
        return helper_create_class(db, statement);
}
