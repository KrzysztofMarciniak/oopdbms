#ifndef ERROR_TOKENIZER_MAIN_H
#define ERROR_TOKENIZER_MAIN_H

/**
 * @file main.h
 * @brief Tokenizer errors.
 */

#include "interface/tokenizer/token.h"

/**
 * @brief What went wrong while tokenizing.
 */
enum tokenizer_error_type {
        /** No error. */
        TOKENIZER_OK = 0,
        /** A character that is not part of the syntax, e.g. @ or a lone !. */
        TOKENIZER_ERROR_ILLEGAL_CHARACTER,
        /** A quote that is never closed. */
        TOKENIZER_ERROR_UNTERMINATED_TEXT,
        /** A number glued to letters, e.g. 12abc. */
        TOKENIZER_ERROR_BAD_NUMBER,
        /** CREATE, DROP, ALTER, LIST or ADD without a valid second word. */
        TOKENIZER_ERROR_INCOMPLETE_PHRASE,
        /** Allocation failed. */
        TOKENIZER_ERROR_OUT_OF_MEMORY
};

/**
 * @brief Tokenizer result.
 */
struct tokenizer_error {
        /** What went wrong, TOKENIZER_OK if nothing. */
        enum tokenizer_error_type type;
        /** Where it went wrong, only meaningful when type is not OK. */
        struct tokenizer_position position;
};

/**
 * @brief Print a tokenizer error.
 *
 * @param te Error to print.
 */
void tokenizer_error_print(struct tokenizer_error te);

#endif
