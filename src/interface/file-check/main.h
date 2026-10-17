#ifndef INTERFACE_FILE_CHECK_MAIN_H
#define INTERFACE_FILE_CHECK_MAIN_H

#include <stdbool.h>

/**
 * @file main.h
 * @brief File check interface, step 3 between analyzer and execution.
 */

#include "error/file-check/main.h"
#include "interface/analyzer/statement.h"

/**
 * @brief State of the database file after a check.
 */
enum FILE_CHECK_STATE {
        /** The check failed, see the error. */
        FILE_CHECK_STATE_FAILED = 0,
        /** The file existed and has a valid header. */
        FILE_CHECK_STATE_READY,
        /** The file did not exist and was just created with a valid header. */
        FILE_CHECK_STATE_CREATED
};

/**
 * @brief Path of the database file.
 *
 * @return The OOPDB environment variable if it is set and not empty,
 *         STORAGE_DEFAULT_PATH otherwise.
 */
const char* i_file_check_path(void);

/**
 * @brief Whether a statement only reads the database.
 *
 * @param kind Statement kind.
 * @return True for commands that require read-only access.
 */
bool i_file_check_statement_is_read_only(enum ANALYZER_STATEMENT kind);

/**
 * @brief Check that the database file can run the statement.
 *
 * - FIND, LIST, DESCRIBE and CALL only read, so the data must already exist:
 *   the file is opened read-only and a missing file is an error.
 * - Every other statement writes, so a missing file is created with a valid
 *   header (the first and only thing this function ever writes). An existing
 *   file is opened read-write, so a missing permission shows up here.
 * - The header (magic and version) of an existing file must be valid.
 *
 * @param path      File to check, see i_file_check_path().
 * @param statement Statement that is about to run.
 * @param error     Receives the result, must not be NULL.
 * @return FILE_CHECK_STATE_READY or FILE_CHECK_STATE_CREATED, or
 *         FILE_CHECK_STATE_FAILED with error->type set.
 */
enum FILE_CHECK_STATE i_file_check(const char* path,
                                   const struct analyzer_statement* statement,
                                   struct file_check_error* error);

#endif// INTERFACE_FILE_CHECK_MAIN_H
