#include "error/analyzer/main.h"

#include <stdio.h>

#include "interface/tokenizer/main.h"

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
                        if (ae.expected == TOKENIZER_ILLEGAL) {
                                fprintf(stderr, "analyzer: unexpected %s",
                                        i_tokenizer_token_name(ae.found));
                        } else {
                                fprintf(stderr,
                                        "analyzer: expected %s but found %s",
                                        i_tokenizer_token_name(ae.expected),
                                        i_tokenizer_token_name(ae.found));
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
