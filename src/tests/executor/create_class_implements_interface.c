/**
 * @file create_class_implements_interface.c
 * @brief Tests creating an interface before a class that implements it.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interface/executor/executor.h"
#include "interface/storage/database.h"
#include "interface/storage/page.h"

#define TEST_FILE "build/create-class-implements-interface-test.oopdb"

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

static uint16_t _read_u16(const uint8_t* bytes) {
        return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
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

static void _test_interface_then_implementing_class(void) {
        struct database db;
        struct analyzer_field interface_fields[] = {
            {.name = "name", .type = {.kind = ANALYZER_TYPE_TEXT}},
        };
        struct analyzer_statement interface_statement = {
            .kind   = ANALYZER_CREATE_INTERFACE,
            .group  = ANALYZER_GROUP_SCHEMA,
            .name   = "Named",
            .fields = {.items = interface_fields, .count = 1, .capacity = 1},
        };
        char* interfaces[]                   = {"Named"};
        struct analyzer_field class_fields[] = {
            {.name = "name", .type = {.kind = ANALYZER_TYPE_TEXT}},
        };
        struct analyzer_statement class_statement = {
            .kind       = ANALYZER_CREATE_CLASS,
            .group      = ANALYZER_GROUP_SCHEMA,
            .name       = "Person",
            .implements = {.items = interfaces, .count = 1, .capacity = 1},
            .fields     = {.items = class_fields, .count = 1, .capacity = 1},
        };
        uint32_t page_no;
        uint16_t offset;
        uint16_t length;
        uint8_t page[DB_PAGE_SIZE];
        size_t position;

        remove(TEST_FILE);
        database_init(&db, TEST_FILE, true);

        CHECK(i_execute(&db, &interface_statement) == EXECUTOR_OK,
              "creates interface before class");
        CHECK(db.hdr.iface_count == 1, "interface count increments");
        CHECK(i_execute(&db, &class_statement) == EXECUTOR_OK,
              "creates class implementing the existing interface");
        CHECK(db.hdr.class_count == 1, "class count increments");
        CHECK(db_interface_dir_find(&db, "Named", &page_no, &offset, &length),
              "implemented interface remains indexed");
        CHECK(db_class_dir_find(&db, "Person", &page_no, &offset, &length),
              "implementing class is indexed");
        CHECK(offset == 0, "class definition starts at page offset zero");
        CHECK(db_read_page(&db, page_no, page),
              "implementing class definition is readable");

        CHECK(_read_u16(page) == 1, "class declares one field");
        position = 2;
        position = _read_string(page, position, "text");
        position = _read_string(page, position, "name");
        CHECK(_read_u16(page + position) == 0, "class has no parent class");
        position += 2;
        CHECK(_read_u16(page + position) == 1,
              "class records one implemented interface");
        position += 2;
        position = _read_string(page, position, "Named");
        CHECK(position == length, "definition length matches stored bytes");

        database_close(&db);
        database_init(&db, TEST_FILE, false);
        CHECK(db_interface_dir_find(&db, "Named", &page_no, &offset, &length),
              "interface remains indexed after reopening");
        CHECK(db_class_dir_find(&db, "Person", &page_no, &offset, &length),
              "implementing class remains indexed after reopening");
        database_close(&db);
        remove(TEST_FILE);
}

int main(void) {
        _test_interface_then_implementing_class();

        printf("create_class_implements_interface tests:\n");
        printf("%d checks, %d failures\n", checks, failures);
        return failures != 0;
}
