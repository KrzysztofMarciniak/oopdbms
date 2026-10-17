/**
 * @file main.c
 * @brief Executor unit tests, run with `make test`.
 */

#include <stdio.h>
#include <string.h>

#include "interface/executor/executor.h"
#include "interface/storage/page.h"

#define TEST_FILE "build/executor-test.oopdb"

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

/** Every statement kind and the group it belongs to. */
static const struct {
        enum ANALYZER_STATEMENT kind;
        enum ANALYZER_GROUP group;
} KINDS[] = {
    {ANALYZER_CREATE_CLASS, ANALYZER_GROUP_SCHEMA},
    {ANALYZER_DROP_CLASS, ANALYZER_GROUP_SCHEMA},
    {ANALYZER_ALTER_CLASS_ADD_FIELD, ANALYZER_GROUP_SCHEMA},
    {ANALYZER_ALTER_CLASS_DROP_FIELD, ANALYZER_GROUP_SCHEMA},
    {ANALYZER_ALTER_CLASS_RENAME_FIELD, ANALYZER_GROUP_SCHEMA},
    {ANALYZER_ALTER_CLASS_RENAME, ANALYZER_GROUP_SCHEMA},
    {ANALYZER_ALTER_CLASS_EXTENDS, ANALYZER_GROUP_SCHEMA},
    {ANALYZER_ALTER_CLASS_IMPLEMENTS, ANALYZER_GROUP_SCHEMA},
    {ANALYZER_CREATE_INTERFACE, ANALYZER_GROUP_SCHEMA},
    {ANALYZER_DROP_INTERFACE, ANALYZER_GROUP_SCHEMA},
    {ANALYZER_CREATE_FUNCTION, ANALYZER_GROUP_FUNCTION},
    {ANALYZER_DROP_FUNCTION, ANALYZER_GROUP_FUNCTION},
    {ANALYZER_CALL, ANALYZER_GROUP_FUNCTION},
    {ANALYZER_NEW, ANALYZER_GROUP_OBJECT},
    {ANALYZER_FIND, ANALYZER_GROUP_OBJECT},
    {ANALYZER_SET, ANALYZER_GROUP_OBJECT},
    {ANALYZER_DELETE, ANALYZER_GROUP_OBJECT},
    {ANALYZER_LIST_CLASSES, ANALYZER_GROUP_METADATA},
    {ANALYZER_LIST_INTERFACES, ANALYZER_GROUP_METADATA},
    {ANALYZER_DESCRIBE, ANALYZER_GROUP_METADATA},
};

#define KIND_COUNT (sizeof KINDS / sizeof KINDS[0])

/** A statement of a kind and group, nothing else filled in. */
static struct analyzer_statement _statement(enum ANALYZER_STATEMENT kind,
                                            enum ANALYZER_GROUP group) {
        struct analyzer_statement statement = {ANALYZER_INVALID};

        statement.kind  = kind;
        statement.group = group;
        return statement;
}

/** Every command kind is recognized and no valid route is unimplemented. */
static void _test_routing(void) {
        struct database db;
        size_t i;

        for (i = 0; i < KIND_COUNT; i++) {
                struct analyzer_statement st =
                    _statement(KINDS[i].kind, KINDS[i].group);
                enum EXECUTOR_RESULT result;

                database_init(&db, "unused.oopdb", false);
                result = i_execute(&db, &st);
                CHECK(result != EXECUTOR_ERROR_NOT_IMPLEMENTED,
                      "a valid kind/group route is implemented");
                CHECK(!database_is_modified(&db),
                      "a command that does nothing leaves the database alone");
        }
}

/** A kind under any group but its own is rejected. */
static void _test_mismatch(void) {
        const enum ANALYZER_GROUP groups[] = {
            ANALYZER_GROUP_SCHEMA, ANALYZER_GROUP_FUNCTION,
            ANALYZER_GROUP_OBJECT, ANALYZER_GROUP_METADATA};
        struct database db;
        size_t i;
        size_t g;

        database_init(&db, "unused.oopdb", false);
        for (i = 0; i < KIND_COUNT; i++) {
                for (g = 0; g < sizeof groups / sizeof groups[0]; g++) {
                        struct analyzer_statement st =
                            _statement(KINDS[i].kind, groups[g]);

                        if (groups[g] == KINDS[i].group) {
                                continue;
                        }
                        CHECK(
                            i_execute(&db, &st) == EXECUTOR_ERROR_BAD_STATEMENT,
                            "a kind under a foreign group is rejected");
                }
        }
}

static void _test_bad_input(void) {
        struct database db;
        struct analyzer_statement none =
            _statement(ANALYZER_INVALID, ANALYZER_GROUP_NONE);
        struct analyzer_statement no_group =
            _statement(ANALYZER_NEW, ANALYZER_GROUP_NONE);
        struct analyzer_statement no_kind =
            _statement(ANALYZER_INVALID, ANALYZER_GROUP_OBJECT);

        database_init(&db, "unused.oopdb", false);
        CHECK(i_execute(&db, &none) == EXECUTOR_ERROR_BAD_STATEMENT,
              "empty statement");
        CHECK(i_execute(&db, &no_group) == EXECUTOR_ERROR_BAD_STATEMENT,
              "kind without a group");
        CHECK(i_execute(&db, &no_kind) == EXECUTOR_ERROR_BAD_STATEMENT,
              "group without a kind");
        CHECK(i_execute(NULL, &none) == EXECUTOR_ERROR_BAD_STATEMENT,
              "no database");
        CHECK(i_execute(&db, NULL) == EXECUTOR_ERROR_BAD_STATEMENT,
              "no statement");
}

/* OLD FORMAT: this test asserts the page-based interface layout. */
#if 0
static void _test_create_interface(void) {
        struct analyzer_field fields[] = {
            {.name = "name", .type = {.kind = ANALYZER_TYPE_TEXT}},
            {.name = "active", .type = {.kind = ANALYZER_TYPE_BOOL}},
        };
        struct analyzer_statement statement = {
            .kind   = ANALYZER_CREATE_INTERFACE,
            .group  = ANALYZER_GROUP_SCHEMA,
            .name   = "Named",
            .fields = {.items = fields, .count = 2, .capacity = 2},
        };
        struct database db;
        uint32_t page;
        uint16_t offset;
        uint16_t length;
        uint8_t definition[DB_PAGE_SIZE];

        remove(TEST_FILE);
        database_init(&db, TEST_FILE, true);
        CHECK(i_execute(&db, &statement) == EXECUTOR_OK,
              "create interface succeeds");
        CHECK(db.hdr.iface_count == 1, "create interface increments count");
        CHECK(db_interface_dir_find(&db, "Named", &page, &offset, &length),
              "create interface is indexed");
        CHECK(offset == 0 && length == 28, "create interface definition size");
        CHECK(db_read_page(&db, page, definition),
              "create interface definition is readable");
        CHECK(get_u16_le(definition) == 2, "create interface field count");
        CHECK(get_u16_le(definition + 2) == 4 &&
                  memcmp(definition + 4, "text", 4) == 0 &&
                  get_u16_le(definition + 8) == 4 &&
                  memcmp(definition + 10, "name", 4) == 0,
              "create interface writes first field");
        CHECK(i_execute(&db, &statement) == EXECUTOR_ERROR_BAD_STATEMENT,
              "create interface rejects duplicate name");
        CHECK(db.hdr.iface_count == 1,
              "duplicate interface leaves count unchanged");
        struct analyzer_field dup_fields[] = {
            {.name = "field1", .type = {.kind = ANALYZER_TYPE_TEXT}},
            {.name = "field1", .type = {.kind = ANALYZER_TYPE_INTEGER}},
        };
        struct analyzer_statement dup_statement = {
            .kind   = ANALYZER_CREATE_INTERFACE,
            .group  = ANALYZER_GROUP_SCHEMA,
            .name   = "DuplicateFields",
            .fields = {.items = dup_fields, .count = 2, .capacity = 2},
        };
        CHECK(i_execute(&db, &dup_statement) == EXECUTOR_ERROR_BAD_STATEMENT,
              "create interface rejects duplicate field names");

        struct analyzer_field invalid_fields[] = {
            {.name = "", .type = {.kind = ANALYZER_TYPE_TEXT}},
        };
        struct analyzer_statement invalid_statement = {
            .kind   = ANALYZER_CREATE_INTERFACE,
            .group  = ANALYZER_GROUP_SCHEMA,
            .name   = "InvalidField",
            .fields = {.items = invalid_fields, .count = 1, .capacity = 1},
        };
        CHECK(
            i_execute(&db, &invalid_statement) == EXECUTOR_ERROR_BAD_STATEMENT,
            "create interface rejects empty field name");

        database_close(&db);

        database_init(&db, TEST_FILE, false);
        CHECK(db_interface_dir_find(&db, "Named", &page, &offset, &length),
              "create interface survives reopen");
        database_close(&db);
        remove(TEST_FILE);
}
#endif

int main(void) {
        _test_routing();
        _test_mismatch();
        _test_bad_input();
        /* OLD PAGE-BASED FORMAT TEST; keep disabled until rewritten. */
        /* _test_create_interface(); */
        printf("executor tests:\n");
        printf("%d checks, %d failures\n", checks, failures);
        return failures != 0;
}
