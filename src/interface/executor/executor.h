#ifndef INTERFACE_EXECUTOR_EXECUTOR_H
#define INTERFACE_EXECUTOR_EXECUTOR_H

/**
 * @file executor.h
 * @brief Executor interface, step 4: run a checked statement.
 */

#include "error/executor/executor_error.h"
#include "interface/analyzer/statement.h"
#include "interface/storage/database.h"

/**
 * @brief Run a statement against the database.
 *
 * Routes on statement->group, then on statement->kind, to the function that
 * does the work. If that succeeded and it changed data, the database is
 * saved before returning.
 *
 * @param db        Database checked in step 3.
 * @param statement Statement from the analyzer.
 * @return EXECUTOR_OK, or what went wrong.
 */
enum EXECUTOR_RESULT i_execute(struct database* db,
                               const struct analyzer_statement* statement);

#endif// INTERFACE_EXECUTOR_EXECUTOR_H
