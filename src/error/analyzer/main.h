#ifndef ERROR_ANALYZER_MAIN_H
#define ERROR_ANALYZER_MAIN_H

/**
 * @file main.h
 * @brief Analyzer errors.
 */

#include "interface/tokenizer/token.h"

/**
 * @brief What went wrong while analyzing.
 */
enum analyzer_error_type {
        /** No error. */
        ANALYZER_OK = 0,
        /** There is no command at all. */
        ANALYZER_ERROR_EMPTY,
        /** A token that does not fit the syntax at that point. */
        ANALYZER_ERROR_UNEXPECTED_TOKEN,
        /** Parentheses or minus signs nested too deeply. */
        ANALYZER_ERROR_TOO_DEEP,
        /** Allocation failed. */
        ANALYZER_ERROR_OUT_OF_MEMORY
};

/**
 * @brief Analyzer result.
 */
struct analyzer_error {
        /** What went wrong, ANALYZER_OK if nothing. */
        enum analyzer_error_type type;
        /** Where the offending token is. */
        struct tokenizer_position position;
        /** The token that was found. */
        enum TOKENIZER_TOKEN found;
        /** The token that was expected, TOKENIZER_ILLEGAL if several fit. */
        enum TOKENIZER_TOKEN expected;
};

/**
 * @brief Print an analyzer error.
 *
 * @param ae Error to print.
 */
void analyzer_error_print(struct analyzer_error ae);

#endif
