#ifndef INTERFACE_ANALYZER_MAIN_H
#define INTERFACE_ANALYZER_MAIN_H

/**
 * @file main.h
 * @brief Analyzer interface.
 */

#include "error/analyzer/main.h"
#include "interface/analyzer/statement.h"
#include "interface/tokenizer/token.h"

/**
 * @brief Check the syntax of a token array and build the statement.
 *
 * Only the grammar is checked. Whether a class exists, whether types match
 * and so on is for the next stage. The statement owns copies of everything it
 * needs, so the tokens can be released right after this call.
 *
 * @param tokens Tokens from i_tokenize(), the last one is TOKENIZER_END.
 * @param error  Receives the result, must not be NULL.
 * @return The statement, kind ANALYZER_INVALID on error. Release with
 *         i_analyzer_free().
 */
struct analyzer_statement i_analyze(const struct tokenizer_tokens* tokens,
                                    struct analyzer_error* error);

/**
 * @brief Release everything a statement owns.
 *
 * @param statement Statement to release, left empty afterwards.
 */
void i_analyzer_free(struct analyzer_statement* statement);

#endif// INTERFACE_ANALYZER_MAIN_H
