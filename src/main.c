#include "interface/analyzer/main.h"

#include <stdio.h>

#include "interface/executor/executor.h"
#include "interface/file-check/main.h"
#include "interface/help/main.h"
#include "interface/storage/database.h"
#include "interface/tokenizer/main.h"

int main(int argc, char** argv) {
        struct tokenizer_error tokenizer_error;
        struct tokenizer_tokens tokens;
        struct analyzer_error analyzer_error;
        struct analyzer_statement statement;
        struct file_check_error file_error;
        enum FILE_CHECK_STATE file_state;
        struct database database;
        enum EXECUTOR_RESULT executed;
        const char* path;

        if (argc < 2) {
                i_print_help();
                return 0;
        }

        tokens = i_tokenize(argc, argv, &tokenizer_error);
        if (tokenizer_error.type != TOKENIZER_OK) {
                tokenizer_error_print(tokenizer_error);
                return 1;
        }

        statement = i_analyze(&tokens, &analyzer_error);
        i_tokenizer_free(&tokens);
        if (analyzer_error.type != ANALYZER_OK) {
                analyzer_error_print(analyzer_error);
                return 1;
        }

        path       = i_file_check_path();
        file_state = i_file_check(path, &statement, &file_error);
        if (file_error.type != FILE_CHECK_OK) {
                file_check_error_print(file_error);
                i_analyzer_free(&statement);
                return 1;
        }

        if (!database_init_format_mode(
                &database, path, file_state == FILE_CHECK_STATE_CREATED,
                i_file_check_statement_is_read_only(statement.kind))) {
                fprintf(stderr, "executor: cannot open database file\n");
                i_analyzer_free(&statement);
                return 1;
        }
        executed = i_execute(&database, &statement);
        database_close(&database);
        if (executed != EXECUTOR_OK) {
                executor_error_print(executed, statement.kind);
                i_analyzer_free(&statement);
                return 1;
        }

        i_analyzer_free(&statement);
        return 0;
}
