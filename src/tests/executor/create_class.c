/**
 * @file create_class.c
 * @brief Tests CREATE CLASS execution and the stored page definition.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interface/executor/executor.h"
#include "interface/storage/database.h"
#include "interface/storage/page.h"

#define TEST_FILE "build/create-class-test.oopdb"

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

static uint16_t _read_u16(const uint8_t* p) {
        return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static size_t _read_string(const uint8_t* page, size_t offset,
                           const char* expected) {
        uint16_t length        = _read_u16(page + offset);
        size_t expected_length = strlen(expected);

        CHECK(length == expected_length, "stored string length matches");
        if (length == expected_length) {
                CHECK(memcmp(page + offset + 2, expected, length) == 0,
                      "stored string matches");
        }
        return offset + 2 + length;
}

static void _test_create_class(void) {
        struct database db;
        struct analyzer_field base_fields[] = {
            {.name = "id", .type = {.kind = ANALYZER_TYPE_INTEGER}},
        };
        struct analyzer_statement base = {
            .kind   = ANALYZER_CREATE_CLASS,
            .group  = ANALYZER_GROUP_SCHEMA,
            .name   = "Base",
            .fields = {.items = base_fields, .count = 1, .capacity = 1},
        };
        struct analyzer_field derived_fields[] = {
            {.name = "owner",
             .type = {.kind = ANALYZER_TYPE_POINTER, .class_name = "Base"}},
            {.name = "age", .type = {.kind = ANALYZER_TYPE_INTEGER}},
        };
        struct analyzer_statement derived = {
            .kind   = ANALYZER_CREATE_CLASS,
            .group  = ANALYZER_GROUP_SCHEMA,
            .name   = "Derived",
            .parent = "Base",
            .fields = {.items = derived_fields, .count = 2, .capacity = 2},
        };
        uint32_t page_no;
        uint16_t offset;
        uint16_t length;
        uint8_t page[DB_PAGE_SIZE];
        size_t position;

        remove(TEST_FILE);
        database_init(&db, TEST_FILE, true);

        CHECK(i_execute(&db, &base) == EXECUTOR_OK,
              "creates a class without parent or interfaces");
        CHECK(i_execute(&db, &derived) == EXECUTOR_OK,
              "creates class with a parent and pointer field");
        CHECK(db.hdr.class_count == 2, "class count increments for each class");
        CHECK(db_class_dir_find(&db, "Derived", &page_no, &offset, &length),
              "new class is indexed");
        CHECK(offset == 0, "class definition starts at page offset zero");
        CHECK(db_read_page(&db, page_no, page), "class definition is readable");
        CHECK(_read_u16(page) == 2, "class definition stores field count");

        position = 2;
        position = _read_string(page, position, "pointer Base");
        position = _read_string(page, position, "owner");
        position = _read_string(page, position, "integer");
        position = _read_string(page, position, "age");
        position = _read_string(page, position, "Base");
        CHECK(_read_u16(page + position) == 0,
              "class definition stores empty interface list");
        position += 2;
        CHECK(position == length, "directory definition length matches bytes");

        CHECK(i_execute(&db, &derived) == EXECUTOR_ERROR_BAD_STATEMENT,
              "rejects duplicate class name");
        CHECK(db.hdr.class_count == 2,
              "duplicate class does not change class count");

        database_close(&db);
        database_init(&db, TEST_FILE, false);
        CHECK(db_class_dir_find(&db, "Derived", &page_no, &offset, &length),
              "class remains indexed after reopening");
        database_close(&db);
        remove(TEST_FILE);
}

static void _test_invalid_class_declarations(void) {
        struct database db;
        struct analyzer_field field[] = {
            {.name = "value", .type = {.kind = ANALYZER_TYPE_TEXT}},
        };
        struct analyzer_statement statement = {
            .kind   = ANALYZER_CREATE_CLASS,
            .group  = ANALYZER_GROUP_SCHEMA,
            .name   = "Invalid",
            .fields = {.items = field, .count = 1, .capacity = 1},
        };
        struct analyzer_field interface_field[] = {
            {.name = "name", .type = {.kind = ANALYZER_TYPE_TEXT}},
        };
        struct analyzer_statement interface_statement = {
            .kind   = ANALYZER_CREATE_INTERFACE,
            .group  = ANALYZER_GROUP_SCHEMA,
            .name   = "Named",
            .fields = {.items = interface_field, .count = 1, .capacity = 1},
        };
        uint32_t page_no;
        uint16_t offset;
        uint16_t length;
        char oversized_name[DB_PAGE_SIZE];

        remove(TEST_FILE);
        database_init(&db, TEST_FILE, true);

        CHECK(i_execute(NULL, &statement) == EXECUTOR_ERROR_BAD_STATEMENT,
              "rejects a null database");
        CHECK(i_execute(&db, NULL) == EXECUTOR_ERROR_BAD_STATEMENT,
              "rejects a null statement");

        statement.name = NULL;
        CHECK(i_execute(&db, &statement) == EXECUTOR_ERROR_BAD_STATEMENT,
              "rejects a null class name");
        statement.name = "";
        CHECK(i_execute(&db, &statement) == EXECUTOR_ERROR_BAD_STATEMENT,
              "rejects an empty class name");
        statement.name = "Invalid";

        statement.parent = "Missing";
        CHECK(i_execute(&db, &statement) == EXECUTOR_ERROR_BAD_STATEMENT,
              "rejects a missing parent class");
        statement.parent = "";
        CHECK(i_execute(&db, &statement) == EXECUTOR_ERROR_BAD_STATEMENT,
              "rejects an empty parent class name");
        statement.parent = NULL;

        CHECK(i_execute(&db, &interface_statement) == EXECUTOR_OK,
              "creates interface for implementation-list validation");
        {
                char* missing_interface[]  = {"Missing"};
                statement.implements.items = missing_interface;
                statement.implements.count = 1;
                CHECK(
                    i_execute(&db, &statement) == EXECUTOR_ERROR_BAD_STATEMENT,
                    "rejects a missing implemented interface");
                statement.implements.count = 0;
                statement.implements.items = NULL;
        }
        {
                char* empty_interface[]    = {""};
                statement.implements.items = empty_interface;
                statement.implements.count = 1;
                CHECK(
                    i_execute(&db, &statement) == EXECUTOR_ERROR_BAD_STATEMENT,
                    "rejects an empty interface name");
        }
        {
                char* null_interface[]     = {NULL};
                statement.implements.items = null_interface;
                CHECK(
                    i_execute(&db, &statement) == EXECUTOR_ERROR_BAD_STATEMENT,
                    "rejects a null interface name");
        }
        {
                char* duplicate_interfaces[] = {"Named", "Named"};
                statement.implements.items   = duplicate_interfaces;
                statement.implements.count   = 2;
                CHECK(
                    i_execute(&db, &statement) == EXECUTOR_ERROR_BAD_STATEMENT,
                    "rejects duplicate implemented interfaces");
        }
        statement.implements.items = NULL;
        statement.implements.count = 0;

        statement.fields.items = NULL;
        statement.fields.count = 1;
        CHECK(i_execute(&db, &statement) == EXECUTOR_ERROR_BAD_STATEMENT,
              "rejects a missing field array");
        statement.fields.items = field;
        statement.fields.count = 0;
        CHECK(i_execute(&db, &statement) == EXECUTOR_ERROR_BAD_STATEMENT,
              "rejects a class with no fields");
        statement.fields.count = 1;

        field[0].type.kind       = ANALYZER_TYPE_POINTER;
        field[0].type.class_name = "Missing";
        CHECK(i_execute(&db, &statement) == EXECUTOR_ERROR_BAD_STATEMENT,
              "rejects a pointer to a missing class");
        field[0].type.class_name = "";
        CHECK(i_execute(&db, &statement) == EXECUTOR_ERROR_BAD_STATEMENT,
              "rejects a pointer without a target class");
        field[0].type.class_name = NULL;
        CHECK(i_execute(&db, &statement) == EXECUTOR_ERROR_BAD_STATEMENT,
              "rejects a pointer with no target class name");
        field[0].type.kind       = ANALYZER_TYPE_TEXT;
        field[0].type.class_name = NULL;

        field[0].type.kind = (enum ANALYZER_TYPE)99;
        CHECK(i_execute(&db, &statement) == EXECUTOR_ERROR_BAD_STATEMENT,
              "rejects an unsupported field type");
        field[0].type.kind = ANALYZER_TYPE_TEXT;

        field[0].name = NULL;
        CHECK(i_execute(&db, &statement) == EXECUTOR_ERROR_BAD_STATEMENT,
              "rejects a null field name");
        field[0].name = "";
        CHECK(i_execute(&db, &statement) == EXECUTOR_ERROR_BAD_STATEMENT,
              "rejects an empty field name");
        field[0].name = "value";

        {
                struct analyzer_field duplicate_fields[] = {
                    {.name = "same", .type = {.kind = ANALYZER_TYPE_TEXT}},
                    {.name = "same", .type = {.kind = ANALYZER_TYPE_INTEGER}},
                };
                statement.fields.items = duplicate_fields;
                statement.fields.count = 2;
                CHECK(
                    i_execute(&db, &statement) == EXECUTOR_ERROR_BAD_STATEMENT,
                    "rejects duplicate field names");
        }

        memset(oversized_name, 'x', sizeof(oversized_name));
        oversized_name[sizeof(oversized_name) - 1] = '\0';
        statement.name                             = oversized_name;
        CHECK(i_execute(&db, &statement) == EXECUTOR_ERROR_BAD_STATEMENT,
              "rejects a class name too large for a directory page");

        {
                char oversized_field[DB_PAGE_SIZE];
                struct analyzer_field large_field[] = {
                    {.name = oversized_field,
                     .type = {.kind = ANALYZER_TYPE_TEXT}},
                };
                memset(oversized_field, 'x', sizeof(oversized_field));
                oversized_field[DB_PAGE_SIZE - 13] = '\0';
                statement.name                     = "OversizedDefinition";
                statement.fields.items             = large_field;
                statement.fields.count             = 1;
                CHECK(
                    i_execute(&db, &statement) == EXECUTOR_ERROR_BAD_STATEMENT,
                    "rejects a definition too large for a page");
                CHECK(!db_class_dir_find(&db, statement.name, &page_no, &offset,
                                         &length),
                      "oversized definition is not registered");
        }

        CHECK(db.hdr.class_count == 0,
              "invalid class declarations do not increment class count");

        database_close(&db);
        remove(TEST_FILE);
}

int main(void) {
        _test_create_class();
        _test_invalid_class_declarations();

        printf("create_class tests:\n");
        printf("%d checks, %d failures\n", checks, failures);
        return failures != 0;
}
