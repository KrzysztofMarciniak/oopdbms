#include "error/tokenizer/main.h"

#include "interface/help/main.h"
#include "interface/tokenizer/main.h"

int main(int argc, char** argv) {
        struct tokenizer_error error;
        struct tokenizer_tokens tokens;

        if (argc < 2) {
                i_print_help();
        }

        // 1. tokenizer
        // returns the structure to pass to analyzer
        tokens = i_tokenize(argc, argv, &error);
        if (error.type != TOKENIZER_OK) {
                tokenizer_error_print(error);
                return 1;
        }

        // 2. (syntax) analyzer
        // 2.1 wrong->print error
        // 2.2 correct->next
        // 3.	 file_check
        // 4.  execute

        i_tokenizer_free(&tokens);
        return 0;
}
