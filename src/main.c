#include "error/analyzer/main.h"

#include "error/file-check/main.h"
#include "error/tokenizer/main.h"
#include "interface/analyzer/main.h"
#include "interface/file-check/main.h"
#include "interface/help/main.h"
#include "interface/tokenizer/main.h"

int main(int argc, char** argv) {
        struct tokenizer_error tokenizer_error;
        struct tokenizer_tokens tokens;
        struct analyzer_error analyzer_error;
        struct analyzer_statement statement;
        struct file_check_error file_error;
        enum FILE_CHECK_STATE file_state;

        if (argc < 2) {
                i_print_help();
                return 0;
        }

        // 1. tokenizer
        tokens = i_tokenize(argc, argv, &tokenizer_error);
        if (tokenizer_error.type != TOKENIZER_OK) {
                tokenizer_error_print(tokenizer_error);
                return 1;
        }

        // 2. (syntax) analyzer
        // 2.1 wrong->print error
        // 2.2 correct->next
        statement = i_analyze(&tokens, &analyzer_error);
        i_tokenizer_free(&tokens);
        if (analyzer_error.type != ANALYZER_OK) {
                analyzer_error_print(analyzer_error);
                return 1;
        }

        // 3.	 file_check
        file_state = i_file_check(i_file_check_path(), &statement, &file_error);
        if (file_error.type != FILE_CHECK_OK) {
                file_check_error_print(file_error);
                i_analyzer_free(&statement);
                return 1;
        }

        // 4.  execute (file_state tells whether the file was just created)
        (void)file_state;

        i_analyzer_free(&statement);
        return 0;
}
