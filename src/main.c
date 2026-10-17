#include "error/analyzer/main.h"

#include <stdio.h>

#include "error/executor/executor_error.h"
#include "error/file-check/main.h"
#include "error/tokenizer/main.h"
#include "interface/analyzer/main.h"
#include "interface/executor/executor.h"
#include "interface/file-check/main.h"
#include "interface/help/main.h"
#include "interface/storage/database.h"
#include "interface/tokenizer/main.h"

static bool _statement_is_read_only(enum ANALYZER_STATEMENT kind) {
        return kind == ANALYZER_FIND || kind == ANALYZER_LIST_CLASSES ||
               kind == ANALYZER_LIST_INTERFACES || kind == ANALYZER_DESCRIBE ||
               kind == ANALYZER_CALL;
}

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
        path       = i_file_check_path();
        file_state = i_file_check(path, &statement, &file_error);
        if (file_error.type != FILE_CHECK_OK) {
                file_check_error_print(file_error);
                i_analyzer_free(&statement);
                return 1;
        }

        // 4.  execute
        if (!database_init_format_mode(
                &database, path, file_state == FILE_CHECK_STATE_CREATED,
                _statement_is_read_only(statement.kind))) {
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
