#include "error/tokenizer/main.h"

#include <stdio.h>

void tokenizer_error_print(struct tokenizer_error te) {
        switch (te.type) {
                case TOKENIZER_OK:
                        puts("tokenizer: OK");
                        break;
                default:
                        puts("Unknown tokenizer error");
                        break;
        }
}
