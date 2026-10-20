/**
 * @file function_dsl.c
 * @brief End-to-end tests for compound function bodies.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interface/analyzer/main.h"
#include "interface/executor/executor.h"
#include "interface/storage/database.h"
#include "interface/tokenizer/main.h"

#define TEST_FILE "build/function-dsl-test.oopdb"

static int checks   = 0;
static int failures = 0;

#define CHECK(condition, name)                                         \
        do {                                                           \
                checks++;                                              \
                if (!(condition)) {                                    \
                        failures++;                                    \
                        printf("FAIL: %s (%s:%d)\n", (name), __FILE__, \
                               __LINE__);                              \
                }                                                      \
        } while (0)

static enum EXECUTOR_RESULT _command(struct database* db, const char* command,
                                     char* output, size_t capacity) {
        char* argv[] = {"oopdbms", (char*)command};
        struct tokenizer_error tokenizer_error;
        struct tokenizer_tokens tokens = i_tokenize(2, argv, &tokenizer_error);
        struct analyzer_error analyzer_error;
        struct analyzer_statement statement;
        enum EXECUTOR_RESULT result;
        FILE* stream;
        long length;
        size_t read_count;

        if (tokenizer_error.type != TOKENIZER_OK) {
                i_tokenizer_free(&tokens);
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }
        statement = i_analyze(&tokens, &analyzer_error);
        i_tokenizer_free(&tokens);
        if (analyzer_error.type != ANALYZER_OK) {
                i_analyzer_free(&statement);
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }
        stream = tmpfile();
        if (stream == NULL) {
                i_analyzer_free(&statement);
                return EXECUTOR_ERROR_OUTPUT_FAILED;
        }
        result = i_execute_to(db, &statement, stream);
        i_analyzer_free(&statement);
        if (result == EXECUTOR_OK &&
            (fflush(stream) != 0 || fseek(stream, 0, SEEK_END) != 0 ||
             (length = ftell(stream)) < 0 ||
             (unsigned long)length >= capacity ||
             fseek(stream, 0, SEEK_SET) != 0)) {
                result = EXECUTOR_ERROR_OUTPUT_FAILED;
        }
        if (result == EXECUTOR_OK) {
                read_count = fread(output, 1, (size_t)length, stream);
                if (read_count != (size_t)length || ferror(stream)) {
                        result = EXECUTOR_ERROR_OUTPUT_FAILED;
                } else {
                        output[read_count] = '\0';
                }
        } else if (capacity != 0) {
                output[0] = '\0';
        }
        fclose(stream);
        return result;
}

static void _test_compound_functions(void) {
        struct database db;
        char output[128];
        enum EXECUTOR_RESULT call_result;

        remove(TEST_FILE);
        CHECK(database_init_format(&db, TEST_FILE, true),
              "create DSL test database");
        CHECK(_command(&db, "CREATE CLASS Account (active bool, age integer)",
                       output, sizeof output) == EXECUTOR_OK,
              "create class for function bodies");
        CHECK(_command(&db, "NEW Account (active = true, age = 21)", output,
                       sizeof output) == EXECUTOR_OK,
              "create object for function bodies");
        CHECK(_command(&db,
                       "CREATE FUNCTION Account.is_adult (minimum integer) "
                       "RETURNS bool AS age >= minimum",
                       output, sizeof output) == EXECUTOR_OK,
              "create expression function for nested calls");
        CHECK(_command(&db,
                       "CREATE FUNCTION Account.is_active_adult (minimum "
                       "integer) RETURNS bool AS { return active && "
                       "is_adult(minimum); }",
                       output, sizeof output) == EXECUTOR_OK,
              "create function calling another function");
        CHECK(_command(&db,
                       "CREATE FUNCTION Account.is_active_expression (minimum "
                       "integer) RETURNS bool AS active && is_adult(minimum)",
                       output, sizeof output) == EXECUTOR_OK,
              "create expression function with logic and a nested call");
        CHECK(_command(&db,
                       "CREATE FUNCTION Account.count_to (limit integer) "
                       "RETURNS bool AS { local count = 0; for (local i = 0; "
                       "i < limit; i = i + 1) { count = count + 1; } if "
                       "(active && age >= limit && count = limit) { return "
                       "! false; } else { return false; } }",
                       output, sizeof output) == EXECUTOR_OK,
              "create function with locals, a for loop, branches, and logic");
        CHECK(_command(&db,
                       "CREATE FUNCTION Account.count_while (limit integer) "
                       "RETURNS bool AS { local count = 0; while (count < "
                       "limit) { count = count + 1; } return false || "
                       "(! false && count = limit); }",
                       output, sizeof output) == EXECUTOR_OK,
              "create function with a while loop and logical operators");
        CHECK(_command(&db, "CALL Account.count_to (18) ON 1", output,
                       sizeof output) == EXECUTOR_OK &&
                  strcmp(output, "true\n") == 0,
              "execute for-loop function and return boolean");
        CHECK(_command(&db, "CALL Account.count_while (18) ON 1", output,
                       sizeof output) == EXECUTOR_OK &&
                  strcmp(output, "true\n") == 0,
              "execute while-loop function");
        CHECK(
            _command(&db,
                     "CREATE FUNCTION Account.after_if (value integer) RETURNS "
                     "bool AS { local eligible = false; if (active) { } "
                     "eligible = true; return eligible; }",
                     output, sizeof output) == EXECUTOR_OK,
            "parse a local assignment immediately after an if block");
        CHECK(_command(&db, "CALL Account.after_if (1) ON 1", output,
                       sizeof output) == EXECUTOR_OK &&
                  strcmp(output, "true\n") == 0,
              "execute after-if assignment");
        CHECK(_command(&db,
                       "CREATE FUNCTION Account.label (value integer) RETURNS "
                       "text AS { local label = \"ok\"; return label; }",
                       output, sizeof output) == EXECUTOR_OK,
              "create a compound function returning text");
        CHECK(_command(&db, "CALL Account.label (1) ON 1", output,
                       sizeof output) == EXECUTOR_OK &&
                  strcmp(output, "\"ok\"\n") == 0,
              "return local text safely");
        CHECK(_command(&db, "CALL Account.is_active_adult (18) ON 1", output,
                       sizeof output) == EXECUTOR_OK &&
                  strcmp(output, "true\n") == 0,
              "execute nested same-owner function call");
        CHECK(_command(&db, "CALL Account.is_active_expression (18) ON 1",
                       output, sizeof output) == EXECUTOR_OK &&
                  strcmp(output, "true\n") == 0,
              "execute nested same-owner call in an expression body");
        CHECK(_command(&db, "NEW Account (active = false, age = 15)", output,
                       sizeof output) == EXECUTOR_OK,
              "create object for else-branch execution");
        call_result =
            _command(&db, "CALL Account.is_active_expression (18) ON 2", output,
                     sizeof output);
        CHECK(call_result == EXECUTOR_OK,
              "execute a nested call in an expression body");
        CHECK(strcmp(output, "false\n") == 0,
              "short-circuit a nested call in an expression body");
        CHECK(_command(&db, "CALL Account.count_to (18) ON 2", output,
                       sizeof output) == EXECUTOR_OK &&
                  strcmp(output, "false\n") == 0,
              "execute else branch and return false");
        CHECK(_command(&db,
                       "CREATE FUNCTION Account.short_circuit (value integer) "
                       "RETURNS bool AS { return false && (1 / 0 = 0); }",
                       output, sizeof output) == EXECUTOR_OK,
              "validate short-circuited logical expression");
        CHECK(_command(&db, "CALL Account.short_circuit (1) ON 1", output,
                       sizeof output) == EXECUTOR_OK &&
                  strcmp(output, "false\n") == 0,
              "short-circuit skips division by zero");
        CHECK(_command(&db,
                       "CREATE FUNCTION Account.bad_condition (value integer) "
                       "RETURNS bool AS { if (age) { return true; } else { "
                       "return false; } }",
                       output, sizeof output) == EXECUTOR_ERROR_BAD_STATEMENT,
              "reject non-boolean conditional");
        CHECK(_command(&db,
                       "CREATE FUNCTION Account.missing_return (value integer) "
                       "RETURNS bool AS { if (active) { return true; } }",
                       output, sizeof output) == EXECUTOR_ERROR_BAD_STATEMENT,
              "reject a function without a return on every path");
        CHECK(
            _command(&db,
                     "CREATE FUNCTION Account.bad_return_type (value integer) "
                     "RETURNS bool AS { if (active) { return true; } else { "
                     "return 1; } }",
                     output, sizeof output) == EXECUTOR_ERROR_BAD_STATEMENT,
            "type-check returns in both branches");
        CHECK(
            _command(&db,
                     "CREATE FUNCTION Account.no_shell (value integer) RETURNS "
                     "bool AS { return system(\"true\"); }",
                     output, sizeof output) == EXECUTOR_ERROR_BAD_STATEMENT,
            "do not execute shell commands");
        CHECK(_command(&db,
                       "CREATE FUNCTION Account.unbounded (value integer) "
                       "RETURNS bool AS { while (true) { } return true; }",
                       output, sizeof output) == EXECUTOR_ERROR_BAD_STATEMENT,
              "stop a loop that exceeds its execution budget");
        database_close(&db);
        remove(TEST_FILE);
}

int test_executor_function_dsl(void) {
        _test_compound_functions();
        printf("function_dsl tests:\n");
        printf("%d checks, %d failures\n", checks, failures);
        return failures != 0;
}
