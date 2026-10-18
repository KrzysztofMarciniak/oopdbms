#include "error/analyzer/main.h"

#include <stdio.h>

#include "interface/tokenizer/main.h"

/** Print the offending token's source spelling without raw control bytes. */
static void _print_found(const struct analyzer_error* ae) {
        size_t i;
        size_t length;
        size_t capacity = sizeof ae->found_text - 1;

        if (ae->found == TOKENIZER_END) {
                fputs("end of input", stderr);
                return;
        }
        fputc('\'', stderr);
        length =
            ae->found_text_length < capacity ? ae->found_text_length : capacity;
        for (i = 0; i < length; i++) {
                unsigned char character = (unsigned char)ae->found_text[i];

                if (character == '\\' || character == '\'') {
                        fputc('\\', stderr);
                        fputc(character, stderr);
                } else if (character >= 0x20 && character <= 0x7e) {
                        fputc(character, stderr);
                } else {
                        fprintf(stderr, "\\x%02x", character);
                }
        }
        if (ae->found_text_length > capacity) {
                fputs("...", stderr);
        }
        fprintf(stderr, "' (%s)", i_tokenizer_token_name(ae->found));
}

/** Describe common alternatives without exposing parser implementation. */
static const char* _expected_kind_name(enum analyzer_expected_kind kind) {
        switch (kind) {
                case ANALYZER_EXPECT_TYPE:
                        return "a type (integer, float, text, bool, or "
                               "pointer)";
                case ANALYZER_EXPECT_LITERAL:
                        return "a literal (integer, float, text, true, or "
                               "false)";
                case ANALYZER_EXPECT_NUMBER:
                        return "a number (integer or float)";
                case ANALYZER_EXPECT_EXPRESSION:
                        return "an expression (a name, literal, or '(')";
                case ANALYZER_EXPECT_COMPARISON:
                        return "a comparison operator (=, !=, <, <=, >, or >=)";
                case ANALYZER_EXPECT_ALTER_ACTION:
                        return "an ALTER CLASS action (ADD FIELD, DROP FIELD, "
                               "RENAME FIELD, RENAME, EXTENDS, or IMPLEMENTS)";
                case ANALYZER_EXPECT_STATEMENT:
                        return "a command (CREATE, DROP, ALTER, CALL, NEW, "
                               "FIND, "
                               "SET, DELETE, LIST, or DESCRIBE)";
                default:
                        return NULL;
        }
}

/** Print the part of the message that says where, if there is a place. */
static void _print_where(const struct analyzer_error* ae) {
        if (ae->found == TOKENIZER_END) {
                fputc('\n', stderr);
                return;
        }
        fprintf(stderr, " at argument %zu, offset %zu\n",
                ae->position.argv_index, ae->position.offset);
}

void analyzer_error_print(struct analyzer_error ae) {
        switch (ae.type) {
                case ANALYZER_OK:
                        puts("analyzer: OK");
                        break;
                case ANALYZER_ERROR_EMPTY:
                        fputs("analyzer: no command given\n", stderr);
                        break;
                case ANALYZER_ERROR_UNEXPECTED_TOKEN:
                        if (ae.expected != TOKENIZER_ILLEGAL) {
                                fprintf(stderr,
                                        "analyzer: expected %s but found ",
                                        i_tokenizer_token_name(ae.expected));
                                _print_found(&ae);
                        } else {
                                const char* expected =
                                    _expected_kind_name(ae.expected_kind);

                                fputs("analyzer: unexpected ", stderr);
                                _print_found(&ae);
                                if (expected != NULL) {
                                        fprintf(stderr, "; expected %s",
                                                expected);
                                }
                        }
                        _print_where(&ae);
                        break;
                case ANALYZER_ERROR_TOO_DEEP:
                        fputs("analyzer: expression nested too deeply", stderr);
                        _print_where(&ae);
                        break;
                case ANALYZER_ERROR_OUT_OF_MEMORY:
                        fputs("analyzer: out of memory\n", stderr);
                        break;
                default:
                        puts("Unknown analyzer error");
                        break;
        }
}
