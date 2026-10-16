/**
 * @file main.c
 * @brief Executor unit tests, run with `make test`.
 */

#include <stdio.h>

#include "interface/executor/executor.h"

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

/** Every command is routed to its function (not rejected as bad). */
static void _test_routing(void) {
        struct database db;
        size_t i;

        for (i = 0; i < KIND_COUNT; i++) {
                struct analyzer_statement st =
                    _statement(KINDS[i].kind, KINDS[i].group);

                database_init(&db, "unused.oopdb", false);
                CHECK(i_execute(&db, &st) != EXECUTOR_ERROR_BAD_STATEMENT,
                      "a kind in its own group is routed");
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

int main(void) {
        _test_routing();
        _test_mismatch();
        _test_bad_input();

        printf("%d checks, %d failures\n", checks, failures);
        return failures != 0;
}
