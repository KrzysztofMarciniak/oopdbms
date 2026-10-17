/**
 * @file create_interface.c
 * @brief CREATE INTERFACE <n> (<field> <type>[, ...])
 */

#include "implementation/executor/executor_internal.h"
#include "implementation/executor/helper/interface_definition/main.h"

enum EXECUTOR_RESULT execute_create_interface(
    struct database* db, const struct analyzer_statement* statement) {
        return helper_create_interface(db, statement);
}
