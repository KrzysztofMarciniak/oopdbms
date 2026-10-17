/**
 * @file list_interfaces.c
 * @brief Tests LIST INTERFACES and interface metadata-table enumeration.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interface/executor/executor.h"
#include "interface/storage/database.h"

#define TEST_FILE "build/list-interfaces-test.oopdb"

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

int main(void) {
        struct database db;
        struct analyzer_field fields[] = {
            {.name = "value", .type = {.kind = ANALYZER_TYPE_INTEGER}},
        };
        struct analyzer_statement first = {
            .kind   = ANALYZER_CREATE_INTERFACE,
            .group  = ANALYZER_GROUP_SCHEMA,
            .name   = "First",
            .fields = {.items = fields, .count = 1, .capacity = 1},
        };
        struct analyzer_statement second = first;
        struct analyzer_statement list   = {
            .kind  = ANALYZER_LIST_INTERFACES,
            .group = ANALYZER_GROUP_METADATA,
        };

        second.name = "Second";
        remove(TEST_FILE);
        CHECK(database_init_format(&db, TEST_FILE, true),
              "create README-format database");

        CHECK(i_execute(&db, &list) == EXECUTOR_OK,
              "empty interface directory lists successfully");
        CHECK(i_execute(&db, &first) == EXECUTOR_OK, "creates first interface");
        CHECK(i_execute(&db, &second) == EXECUTOR_OK,
              "creates second interface");
        CHECK(i_execute(&db, &list) == EXECUTOR_OK,
              "lists populated interface directory");

        database_close(&db);
        CHECK(database_init_format(&db, TEST_FILE, false),
              "reopen README-format database");
        CHECK(i_execute(&db, &list) == EXECUTOR_OK,
              "lists interfaces after reopening the database");
        database_close(&db);
        remove(TEST_FILE);

        printf("list_interfaces tests:\n");
        printf("%d checks, %d failures\n", checks, failures);
        return failures != 0;
}
