#ifndef INTERFACE_TOKENIZER_MAIN_H
#define INTERFACE_TOKENIZER_MAIN_H

/**
 * @file main.h
 * @brief Tokenizer interface.
 */

#include "error/tokenizer/main.h"
#include "interface/tokenizer/token.h"

/**
 * @brief Split argv into tokens.
 *
 * argv[0] is skipped. Words are split bash-like: whitespace separates them,
 * quotes ("..." or '...', backslash escapes) keep text together, and
 * punctuation and operators split words even without spaces. Each argv entry
 * is scanned on its own, so a quoted text never spans two entries, but a
 * phrase such as CREATE CLASS may.
 *
 * On success the last token is TOKENIZER_END and error->type is
 * TOKENIZER_OK. On failure the returned array is empty and error says what
 * and where.
 *
 * @param argc  Argument count as received by main.
 * @param argv  Argument vector as received by main.
 * @param error Receives the result, must not be NULL.
 * @return Tokens, release with i_tokenizer_free().
 */
struct tokenizer_tokens i_tokenize(int argc, char** argv,
                                   struct tokenizer_error* error);

/**
 * @brief Release the memory held by a token array and its token texts.
 *
 * @param tokens Array to release, left empty afterwards.
 */
void i_tokenizer_free(struct tokenizer_tokens* tokens);

/**
 * @brief Human readable name of a token type, for error messages.
 *
 * @param type Token type.
 * @return Static string, never NULL.
 */
const char* i_tokenizer_token_name(enum TOKENIZER_TOKEN type);

#endif// INTERFACE_TOKENIZER_MAIN_H
