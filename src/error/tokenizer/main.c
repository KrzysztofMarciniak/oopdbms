#include "error/tokenizer/main.h"

#include <stdio.h>

static void _print_at(const char* message, struct tokenizer_position position) {
        fprintf(stderr, "tokenizer: %s at argument %zu, offset %zu\n", message,
                position.argv_index, position.offset);
}

void tokenizer_error_print(struct tokenizer_error te) {
        switch (te.type) {
                case TOKENIZER_OK:
                        puts("tokenizer: OK");
                        break;
                case TOKENIZER_ERROR_ILLEGAL_CHARACTER:
                        _print_at("illegal character", te.position);
                        break;
                case TOKENIZER_ERROR_UNTERMINATED_TEXT:
                        _print_at("unterminated text", te.position);
                        break;
                case TOKENIZER_ERROR_BAD_NUMBER:
                        _print_at("malformed number", te.position);
                        break;
                case TOKENIZER_ERROR_INCOMPLETE_PHRASE:
                        _print_at("incomplete command", te.position);
                        break;
                case TOKENIZER_ERROR_OUT_OF_MEMORY:
                        _print_at("out of memory", te.position);
                        break;
                default:
                        puts("Unknown tokenizer error");
                        break;
        }
}
