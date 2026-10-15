#include "error/tokenizer/main.h"

#include <stdbool.h>
#include <stddef.h>

void i_tokenize(int argc, char** argv) {
        struct tokenizer_error error = {
            .type = TOKENIZER_OK,
        };

        // tokenizer args...

        if (true) {
                error.type = TOKENIZER_OK;
        }

        tokenizer_error_print(error);
}
