/**
 * @file function_metadata.c
 * @brief Tests function listings, descriptions, and boolean function calls.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interface/executor/executor.h"
#include "interface/storage/database.h"

#define TEST_FILE "build/function-metadata-test.oopdb"

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

static enum EXECUTOR_RESULT _capture(struct database* db,
                                     const struct analyzer_statement* statement,
                                     char* buffer, size_t capacity) {
        FILE* output = tmpfile();
        enum EXECUTOR_RESULT result;
        long length = 0;
        size_t bytes_read;

        if (output == NULL) {
                return EXECUTOR_ERROR_OUTPUT_FAILED;
        }
        result = i_execute_to(db, statement, output);
        if (result == EXECUTOR_OK &&
            (fflush(output) != 0 || fseek(output, 0, SEEK_END) != 0 ||
             (length = ftell(output)) < 0 ||
             (unsigned long)length >= capacity ||
             fseek(output, 0, SEEK_SET) != 0)) {
                result = EXECUTOR_ERROR_OUTPUT_FAILED;
        }
        if (result == EXECUTOR_OK) {
                bytes_read = fread(buffer, 1, (size_t)length, output);
                if (bytes_read != (size_t)length || ferror(output)) {
                        result = EXECUTOR_ERROR_OUTPUT_FAILED;
                } else {
                        buffer[bytes_read] = '\0';
                }
        } else if (capacity != 0) {
                buffer[0] = '\0';
        }
        fclose(output);
        return result;
}

static enum EXECUTOR_RESULT _create_function(
    struct database* db, const char* owner, const char* name,
    struct analyzer_field* parameters, size_t parameter_count,
    struct analyzer_type return_type, struct analyzer_expression* expression) {
        struct analyzer_statement statement = {
            .kind        = ANALYZER_CREATE_FUNCTION,
            .group       = ANALYZER_GROUP_FUNCTION,
            .name        = (char*)owner,
            .function    = (char*)name,
            .fields      = {.items    = parameters,
                            .count    = parameter_count,
                            .capacity = parameter_count},
            .return_type = return_type,
            .expression  = expression,
        };

        return i_execute(db, &statement);
}

static void _expect_call(struct database* db, const char* owner,
                         const char* function, struct analyzer_value* value,
                         const char* expected) {
        struct analyzer_statement call = {
            .kind     = ANALYZER_CALL,
            .group    = ANALYZER_GROUP_FUNCTION,
            .name     = (char*)owner,
            .function = (char*)function,
            .id       = "1",
            .values   = {.items = value, .count = 1, .capacity = 1},
        };
        char output[128];

        CHECK(_capture(db, &call, output, sizeof output) == EXECUTOR_OK,
              "CALL executes a boolean or pointer function");
        CHECK(strcmp(output, expected) == 0,
              "CALL prints the function return value");
}

static void _test_function_metadata_and_boolean_calls(void) {
        struct database db;
        struct analyzer_field target_fields[] = {
            {.name = "value", .type = {.kind = ANALYZER_TYPE_INTEGER}},
        };
        struct analyzer_field account_fields[] = {
            {.name = "active", .type = {.kind = ANALYZER_TYPE_BOOL}},
            {.name = "age", .type = {.kind = ANALYZER_TYPE_INTEGER}},
        };
        struct analyzer_statement target_class = {
            .kind   = ANALYZER_CREATE_CLASS,
            .group  = ANALYZER_GROUP_SCHEMA,
            .name   = "Target",
            .fields = {.items = target_fields, .count = 1, .capacity = 1},
        };
        struct analyzer_statement account_class = {
            .kind   = ANALYZER_CREATE_CLASS,
            .group  = ANALYZER_GROUP_SCHEMA,
            .name   = "Account",
            .fields = {.items = account_fields, .count = 2, .capacity = 2},
        };
        struct analyzer_statement list = {
            .kind  = ANALYZER_LIST_FUNCTIONS,
            .group = ANALYZER_GROUP_METADATA,
        };
        struct analyzer_assignment target_assignment[] = {
            {.field = "value",
             .value = {.kind = ANALYZER_VALUE_INTEGER, .text = "10"}},
        };
        struct analyzer_assignment account_assignments[] = {
            {.field = "active",
             .value = {.kind = ANALYZER_VALUE_BOOL, .text = "true"}},
            {.field = "age",
             .value = {.kind = ANALYZER_VALUE_INTEGER, .text = "21"}},
        };
        struct analyzer_statement target_object = {
            .kind        = ANALYZER_NEW,
            .group       = ANALYZER_GROUP_OBJECT,
            .name        = "Target",
            .assignments = {.items    = target_assignment,
                            .count    = 1,
                            .capacity = 1},
        };
        struct analyzer_statement account_object = {
            .kind        = ANALYZER_NEW,
            .group       = ANALYZER_GROUP_OBJECT,
            .name        = "Account",
            .assignments = {.items    = account_assignments,
                            .count    = 2,
                            .capacity = 2},
        };
        struct analyzer_field matches_parameter[] = {
            {.name = "expected", .type = {.kind = ANALYZER_TYPE_BOOL}},
        };
        struct analyzer_expression active_field = {
            .kind  = ANALYZER_EXPRESSION_FIELD,
            .field = "active",
        };
        struct analyzer_expression expected_field = {
            .kind  = ANALYZER_EXPRESSION_FIELD,
            .field = "expected",
        };
        struct analyzer_expression matches_expression = {
            .kind  = ANALYZER_EXPRESSION_BINARY,
            .op    = ANALYZER_OPERATOR_EQUAL,
            .left  = &active_field,
            .right = &expected_field,
        };
        struct analyzer_expression false_literal = {
            .kind  = ANALYZER_EXPRESSION_LITERAL,
            .value = {.kind = ANALYZER_VALUE_BOOL, .text = "false"},
        };
        struct analyzer_expression true_literal = {
            .kind  = ANALYZER_EXPRESSION_LITERAL,
            .value = {.kind = ANALYZER_VALUE_BOOL, .text = "true"},
        };
        struct analyzer_field pointer_parameter[] = {
            {.name = "target",
             .type = {.kind = ANALYZER_TYPE_POINTER, .class_name = "Target"}},
        };
        struct analyzer_expression target_parameter = {
            .kind  = ANALYZER_EXPRESSION_FIELD,
            .field = "target",
        };
        struct analyzer_field positive_parameter[] = {
            {.name = "value", .type = {.kind = ANALYZER_TYPE_INTEGER}},
        };
        struct analyzer_expression value_field = {
            .kind  = ANALYZER_EXPRESSION_FIELD,
            .field = "value",
        };
        struct analyzer_expression zero_literal = {
            .kind  = ANALYZER_EXPRESSION_LITERAL,
            .value = {.kind = ANALYZER_VALUE_INTEGER, .text = "0"},
        };
        struct analyzer_expression positive_expression = {
            .kind  = ANALYZER_EXPRESSION_BINARY,
            .op    = ANALYZER_OPERATOR_GREATER,
            .left  = &value_field,
            .right = &zero_literal,
        };
        struct analyzer_value bool_true = {
            .kind = ANALYZER_VALUE_BOOL,
            .text = "true",
        };
        struct analyzer_value bool_false = {
            .kind = ANALYZER_VALUE_BOOL,
            .text = "false",
        };
        struct analyzer_value pointer_value = {
            .kind       = ANALYZER_VALUE_POINTER,
            .text       = "1",
            .class_name = "Target",
        };
        struct analyzer_value integer_value = {
            .kind = ANALYZER_VALUE_INTEGER,
            .text = "10",
        };
        struct analyzer_statement describe = {
            .kind     = ANALYZER_DESCRIBE_FUNCTION,
            .group    = ANALYZER_GROUP_METADATA,
            .name     = "Account",
            .function = "matches",
        };
        char output[512];

        remove(TEST_FILE);
        CHECK(database_init_format(&db, TEST_FILE, true),
              "create README-format database");
        CHECK(_capture(&db, &list, output, sizeof output) == EXECUTOR_OK &&
                  output[0] == '\0',
              "empty function table lists successfully");
        CHECK(i_execute(&db, &target_class) == EXECUTOR_OK,
              "create pointer target class");
        CHECK(i_execute(&db, &account_class) == EXECUTOR_OK,
              "create function owner class");
        CHECK(i_execute(&db, &target_object) == EXECUTOR_OK,
              "create object for pointer function");
        CHECK(i_execute(&db, &account_object) == EXECUTOR_OK,
              "create object with boolean field");

        CHECK(
            _create_function(&db, "Account", "matches", matches_parameter, 1,
                             (struct analyzer_type){.kind = ANALYZER_TYPE_BOOL},
                             &matches_expression) == EXECUTOR_OK,
            "create bool function from bool field and parameter");
        CHECK(
            _create_function(&db, "Account", "is_active", matches_parameter, 1,
                             (struct analyzer_type){.kind = ANALYZER_TYPE_BOOL},
                             &active_field) == EXECUTOR_OK,
            "create bool function returning a boolean field");
        CHECK(_create_function(
                  &db, "Account", "constant_false", matches_parameter, 1,
                  (struct analyzer_type){.kind = ANALYZER_TYPE_BOOL},
                  &false_literal) == EXECUTOR_OK,
              "create bool function returning a boolean literal");
        CHECK(_create_function(
                  &db, "Account", "constant_true", matches_parameter, 1,
                  (struct analyzer_type){.kind = ANALYZER_TYPE_BOOL},
                  &true_literal) == EXECUTOR_OK,
              "create bool function returning true");
        CHECK(_create_function(
                  &db, "Account", "points_to", pointer_parameter, 1,
                  (struct analyzer_type){.kind       = ANALYZER_TYPE_POINTER,
                                         .class_name = "Target"},
                  &target_parameter) == EXECUTOR_OK,
              "create pointer function for metadata type descriptions");
        CHECK(_create_function(
                  &db, "Target", "is_positive", positive_parameter, 1,
                  (struct analyzer_type){.kind = ANALYZER_TYPE_BOOL},
                  &positive_expression) == EXECUTOR_OK,
              "create boolean function owned by another class");

        CHECK(_capture(&db, &list, output, sizeof output) == EXECUTOR_OK &&
                  strcmp(output,
                         "Account.matches\n"
                         "Account.is_active\n"
                         "Account.constant_false\n"
                         "Account.constant_true\n"
                         "Account.points_to\n"
                         "Target.is_positive\n") == 0,
              "LIST FUNCTIONS prints each function with its owner class");
        CHECK(_capture(&db, &describe, output, sizeof output) == EXECUTOR_OK &&
                  strcmp(output,
                         "Account.matches (expected bool) RETURNS bool AS "
                         "(active=expected)\n") == 0,
              "DESCRIBE FUNCTION prints its typed signature and expression");
        describe.function = "points_to";
        CHECK(_capture(&db, &describe, output, sizeof output) == EXECUTOR_OK &&
                  strcmp(output,
                         "Account.points_to (target pointer Target) RETURNS "
                         "pointer Target AS target\n") == 0,
              "DESCRIBE FUNCTION resolves pointer parameter and return types");
        describe.function = "missing";
        CHECK(_capture(&db, &describe, output, sizeof output) ==
                  EXECUTOR_ERROR_BAD_STATEMENT,
              "DESCRIBE FUNCTION rejects an unknown function");

        _expect_call(&db, "Account", "matches", &bool_true, "true\n");
        _expect_call(&db, "Account", "matches", &bool_false, "false\n");
        _expect_call(&db, "Account", "is_active", &bool_false, "true\n");
        _expect_call(&db, "Account", "constant_false", &bool_true, "false\n");
        _expect_call(&db, "Account", "constant_true", &bool_false, "true\n");
        _expect_call(&db, "Account", "points_to", &pointer_value, "Target.1\n");
        _expect_call(&db, "Target", "is_positive", &integer_value, "true\n");

        database_close(&db);
        CHECK(database_init_format(&db, TEST_FILE, false),
              "reopen function database");
        CHECK(_capture(&db, &list, output, sizeof output) == EXECUTOR_OK &&
                  strcmp(output,
                         "Account.matches\n"
                         "Account.is_active\n"
                         "Account.constant_false\n"
                         "Account.constant_true\n"
                         "Account.points_to\n"
                         "Target.is_positive\n") == 0,
              "LIST FUNCTIONS reads the persisted table after reopening");
        database_close(&db);
        remove(TEST_FILE);
}

int main(void) {
        _test_function_metadata_and_boolean_calls();
        printf("function_metadata tests:\n");
        printf("%d checks, %d failures\n", checks, failures);
        return failures != 0;
}
