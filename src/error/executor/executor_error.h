#ifndef ERROR_EXECUTOR_EXECUTOR_ERROR_H
#define ERROR_EXECUTOR_EXECUTOR_ERROR_H

/**
 * @file executor_error.h
 * @brief Executor results and errors.
 */

#include "interface/analyzer/statement.h"

/**
 * @brief Outcome of executing a statement.
 */
enum EXECUTOR_RESULT {
        /** The statement ran. */
        EXECUTOR_OK = 0,
        /** Missing arguments, or group and kind do not belong together. */
        EXECUTOR_ERROR_BAD_STATEMENT,
        /** The command is routed but its code is not written yet. */
        EXECUTOR_ERROR_NOT_IMPLEMENTED,
        /** The command could not read required database metadata. */
        EXECUTOR_ERROR_STORAGE_FAILED,
        /** The statement ran but the changes could not be saved. */
        EXECUTOR_ERROR_SAVE_FAILED
};

/**
 * @brief Print an executor result.
 *
 * @param result Result to print.
 * @param kind   Statement that was executed, used in the message.
 */
void executor_error_print(enum EXECUTOR_RESULT result,
                          enum ANALYZER_STATEMENT kind);

#endif
