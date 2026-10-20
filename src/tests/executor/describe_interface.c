/**
 * @file describe_interface.c
 * @brief Tests DESCRIBE INTERFACE output and persisted interface fields.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interface/executor/executor.h"
#include "interface/storage/database.h"

#define TEST_FILE "build/describe-interface-test.oopdb"

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

static void _test_describe_interface(void) {
        struct database db;
        struct analyzer_field target_fields[] = {
            {.name = "id", .type = {.kind = ANALYZER_TYPE_INTEGER}},
        };
        struct analyzer_field interface_fields[] = {
            {.name = "name", .type = {.kind = ANALYZER_TYPE_TEXT}},
            {.name = "active", .type = {.kind = ANALYZER_TYPE_BOOL}},
            {.name = "target",
             .type = {.kind = ANALYZER_TYPE_POINTER, .class_name = "Target"}},
        };
        struct analyzer_statement target = {
            .kind   = ANALYZER_CREATE_CLASS,
            .group  = ANALYZER_GROUP_SCHEMA,
            .name   = "Target",
            .fields = {.items = target_fields, .count = 1, .capacity = 1},
        };
        struct analyzer_statement create_interface = {
            .kind   = ANALYZER_CREATE_INTERFACE,
            .group  = ANALYZER_GROUP_SCHEMA,
            .name   = "Named",
            .fields = {.items = interface_fields, .count = 3, .capacity = 3},
        };
        struct analyzer_statement describe = {
            .kind  = ANALYZER_DESCRIBE_INTERFACE,
            .group = ANALYZER_GROUP_METADATA,
            .name  = "Named",
        };
        char output[256];

        remove(TEST_FILE);
        CHECK(database_init_format(&db, TEST_FILE, true),
              "create README-format database");
        CHECK(i_execute(&db, &target) == EXECUTOR_OK,
              "create pointer target class");
        CHECK(i_execute(&db, &create_interface) == EXECUTOR_OK,
              "create interface with typed fields");
        CHECK(_capture(&db, &describe, output, sizeof output) == EXECUTOR_OK &&
                  strcmp(output,
                         "Named\n"
                         "  name text\n"
                         "  active bool\n"
                         "  target pointer Target\n") == 0,
              "DESCRIBE INTERFACE prints the name and every field type");

        describe.name = "Missing";
        CHECK(_capture(&db, &describe, output, sizeof output) ==
                  EXECUTOR_ERROR_BAD_STATEMENT,
              "DESCRIBE INTERFACE rejects an unknown interface");

        database_close(&db);
        CHECK(database_init_format(&db, TEST_FILE, false),
              "reopen interface database");
        describe.name = "Named";
        CHECK(_capture(&db, &describe, output, sizeof output) == EXECUTOR_OK &&
                  strstr(output, "target pointer Target\n") != NULL,
              "DESCRIBE INTERFACE reads persisted field metadata");
        database_close(&db);
        remove(TEST_FILE);
}

int main(void) {
        _test_describe_interface();
        printf("describe_interface tests:\n");
        printf("%d checks, %d failures\n", checks, failures);
        return failures != 0;
}
