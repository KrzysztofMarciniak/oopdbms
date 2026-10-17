/**
 * @file list_interfaces.c
 * @brief Tests LIST INTERFACES and interface-directory enumeration.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interface/executor/executor.h"
#include "interface/storage/database.h"
#include "interface/storage/directory.h"

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

struct collected_names {
        char names[2][16];
        size_t count;
        bool overflow;
};

static void _collect_name(const struct dir_entry* entry, void* context) {
        struct collected_names* collected = context;

        if (collected->count >= 2 ||
            entry->name_length >= sizeof(collected->names[0])) {
                collected->overflow = true;
                return;
        }
        memcpy(collected->names[collected->count], entry->name,
               entry->name_length);
        collected->names[collected->count][entry->name_length] = '\0';
        collected->count++;
}

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
        struct collected_names collected = {0};

        second.name = "Second";
        remove(TEST_FILE);
        database_init(&db, TEST_FILE, true);

        CHECK(i_execute(&db, &list) == EXECUTOR_OK,
              "empty interface directory lists successfully");
        CHECK(i_execute(&db, &first) == EXECUTOR_OK, "creates first interface");
        CHECK(i_execute(&db, &second) == EXECUTOR_OK,
              "creates second interface");
        CHECK(i_execute(&db, &list) == EXECUTOR_OK,
              "lists populated interface directory");
        CHECK(db_dir_visit(&db, IFACE_DIR, _collect_name, &collected),
              "interface directory helper enumerates entries");
        CHECK(!collected.overflow && collected.count == 2,
              "directory helper returns both interface names");
        CHECK(strcmp(collected.names[0], "First") == 0 &&
                  strcmp(collected.names[1], "Second") == 0,
              "directory helper returns interface names in directory order");
        {
                uint32_t iface_dir_page = db.hdr.iface_dir_page;

                db.hdr.iface_dir_page = PAGE_INVALID;
                CHECK(i_execute(&db, &list) == EXECUTOR_ERROR_STORAGE_FAILED,
                      "reports an unreadable interface directory");
                db.hdr.iface_dir_page = iface_dir_page;
        }

        database_close(&db);
        database_init(&db, TEST_FILE, false);
        CHECK(i_execute(&db, &list) == EXECUTOR_OK,
              "lists interfaces after reopening the database");
        database_close(&db);
        remove(TEST_FILE);

        printf("list_interfaces tests:\n");
        printf("%d checks, %d failures\n", checks, failures);
        return failures != 0;
}
