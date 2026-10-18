/**
 * @file commands.c
 * @brief Exercise executor schema, object, and function commands end to end.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "implementation/executor/helper/format_types.h"
#include "interface/executor/executor.h"
#include "interface/storage/database.h"

#define TEST_FILE "build/executor-commands-test.oopdb"
#define MIGRATION_TEST_FILE "build/legacy-migration-test.oopdb"

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

static uint16_t _u16(const uint8_t* bytes) {
        return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

static uint32_t _u32(const uint8_t* bytes) {
        return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
               ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint64_t _u64(const uint8_t* bytes) {
        return (uint64_t)_u32(bytes) | ((uint64_t)_u32(bytes + 4) << 32);
}

static uint8_t* _read_file_from(const char* path, size_t* file_size) {
        FILE* file = fopen(path, "rb");
        long length;
        uint8_t* bytes;

        if (file == NULL || fseek(file, 0, SEEK_END) != 0 ||
            (length = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0) {
                if (file != NULL) {
                        fclose(file);
                }
                return NULL;
        }
        bytes = malloc((size_t)length);
        if (bytes == NULL ||
            fread(bytes, 1, (size_t)length, file) != (size_t)length) {
                free(bytes);
                fclose(file);
                return NULL;
        }
        fclose(file);
        *file_size = (size_t)length;
        return bytes;
}

static uint8_t* _read_file(size_t* file_size) {
        return _read_file_from(TEST_FILE, file_size);
}

static void _write_u64(uint8_t* bytes, uint64_t value) {
        size_t i;

        for (i = 0; i < 8; i++) {
                bytes[i] = (uint8_t)(value >> (i * 8));
        }
}

static void _test_legacy_migration(void) {
        struct database db;
        struct analyzer_field field = {
            .name = "name",
            .type = {.kind = ANALYZER_TYPE_TEXT},
        };
        struct analyzer_value old_value = {
            .kind = ANALYZER_VALUE_TEXT,
            .text = "legacy",
        };
        struct analyzer_value new_value = {
            .kind = ANALYZER_VALUE_TEXT,
            .text = "migrated",
        };
        struct analyzer_assignment assignment = {
            .field = "name",
            .value = old_value,
        };
        struct analyzer_statement statement = {
            .kind   = ANALYZER_CREATE_CLASS,
            .group  = ANALYZER_GROUP_SCHEMA,
            .name   = "Legacy",
            .fields = {.items = &field, .count = 1, .capacity = 1},
        };
        size_t file_size = 0;
        uint8_t* bytes;
        uint64_t table_offset;
        uint64_t entry_offset;
        uint64_t node_offset;
        uint64_t record_offset;
        uint32_t record_length;
        FILE* file;

        remove(MIGRATION_TEST_FILE);
        CHECK(database_init_format(&db, MIGRATION_TEST_FILE, true),
              "initialize legacy migration fixture");
        CHECK(i_execute(&db, &statement) == EXECUTOR_OK,
              "create class before legacy migration");
        statement = (struct analyzer_statement){
            .kind  = ANALYZER_NEW,
            .group = ANALYZER_GROUP_OBJECT,
            .name  = "Legacy",
            .assignments =
                {
                    .items    = &assignment,
                    .count    = 1,
                    .capacity = 1,
                },
        };
        CHECK(i_execute(&db, &statement) == EXECUTOR_OK,
              "create object before legacy migration");
        database_close(&db);

        bytes = _read_file_from(MIGRATION_TEST_FILE, &file_size);
        CHECK(bytes != NULL, "read version 1 migration fixture");
        if (bytes == NULL) {
                remove(MIGRATION_TEST_FILE);
                return;
        }
        table_offset = _u64(bytes + 32);
        entry_offset = table_offset;
        node_offset  = _u64(bytes + entry_offset + 16);
        if (node_offset <= file_size && file_size - (size_t)node_offset >= 12) {
                record_length = _u32(bytes + node_offset + 8);
                record_offset = node_offset + 12;
        } else {
                record_length = 0;
                record_offset = 0;
        }
        CHECK(record_length >= 16 && record_offset <= file_size &&
                  record_length <= file_size - (size_t)record_offset,
              "fixture contains a valid object-log record");
        if (record_length < 16 || record_offset > file_size ||
            record_length > file_size - (size_t)record_offset) {
                free(bytes);
                remove(MIGRATION_TEST_FILE);
                return;
        }
        _write_u64(bytes + entry_offset + 16, record_offset);
        _write_u64(bytes + entry_offset + 24, record_length);
        put_u16_le(bytes + 14, EXECUTOR_FORMAT_VERSION_CONTIGUOUS);
        file = fopen(MIGRATION_TEST_FILE, "r+b");
        CHECK(file != NULL, "open legacy migration fixture for conversion");
        if (file == NULL) {
                free(bytes);
                remove(MIGRATION_TEST_FILE);
                return;
        }
        {
                bool converted = fseek(file, 0, SEEK_SET) == 0;
                if (converted) {
                        converted =
                            fwrite(bytes, 1, file_size, file) == file_size;
                }
                if (fclose(file) != 0) {
                        converted = false;
                }
                CHECK(converted,
                      "convert fixture to version 0 contiguous data");
        }
        free(bytes);

        CHECK(database_init_format(&db, MIGRATION_TEST_FILE, false),
              "open legacy database for migration");
        assignment.value = new_value;
        statement        = (struct analyzer_statement){
            .kind  = ANALYZER_NEW,
            .group = ANALYZER_GROUP_OBJECT,
            .name  = "Legacy",
            .assignments =
                {
                    .items    = &assignment,
                    .count    = 1,
                    .capacity = 1,
                },
        };
        CHECK(i_execute(&db, &statement) == EXECUTOR_OK,
              "first write migrates legacy database and appends an object");
        database_close(&db);

        bytes = _read_file_from(MIGRATION_TEST_FILE, &file_size);
        CHECK(bytes != NULL, "read migrated database");
        if (bytes != NULL) {
                table_offset = _u64(bytes + 32);
                entry_offset = table_offset;
                node_offset  = _u64(bytes + entry_offset + 16);
                CHECK(_u16(bytes + 14) == EXECUTOR_FORMAT_VERSION_OBJECT_LOG,
                      "migration upgrades the format version");
                CHECK(_u64(bytes + entry_offset + 32) == 2,
                      "migration preserves and increments the live count");
                CHECK(node_offset <= file_size &&
                          file_size - (size_t)node_offset >= 12 &&
                          _u64(bytes + node_offset) != 0,
                      "migrated log links the new version to the old record");
                if (node_offset <= file_size &&
                    file_size - (size_t)node_offset >= 12) {
                        record_length = _u32(bytes + node_offset + 8);
                        record_offset = node_offset + 12;
                        CHECK(record_length >= 16 &&
                                  record_offset <= file_size &&
                                  record_length <=
                                      file_size - (size_t)record_offset &&
                                  _u64(bytes + record_offset + 4) == 2,
                              "newest migrated log entry has the next ID");
                }
                free(bytes);
        }
        CHECK(database_init_format_mode(&db, MIGRATION_TEST_FILE, false, true),
              "reopen migrated database read-only");
        statement = (struct analyzer_statement){
            .kind  = ANALYZER_FIND,
            .group = ANALYZER_GROUP_OBJECT,
            .name  = "Legacy",
        };
        CHECK(i_execute(&db, &statement) == EXECUTOR_OK,
              "read migrated and newly written objects");
        database_close(&db);
        remove(MIGRATION_TEST_FILE);
}

static enum EXECUTOR_RESULT _execute(struct database* db,
                                     enum ANALYZER_STATEMENT kind,
                                     enum ANALYZER_GROUP group, char* name,
                                     struct analyzer_statement* statement) {
        statement->kind  = kind;
        statement->group = group;
        statement->name  = name;
        return i_execute(db, statement);
}

static void _schema_and_function_commands(struct database* db) {
        struct analyzer_field named_fields[] = {
            {.name = "name", .type = {.kind = ANALYZER_TYPE_TEXT}},
        };
        struct analyzer_field base_fields[] = {
            {.name = "name", .type = {.kind = ANALYZER_TYPE_TEXT}},
        };
        struct analyzer_field child_fields[] = {
            {.name = "note", .type = {.kind = ANALYZER_TYPE_TEXT}},
        };
        struct analyzer_field duplicate_fields[] = {
            {.name = "name", .type = {.kind = ANALYZER_TYPE_INTEGER}},
        };
        char* named[]                       = {"Named"};
        struct analyzer_statement statement = {0};

        statement.fields.items    = named_fields;
        statement.fields.count    = 1;
        statement.fields.capacity = 1;
        CHECK(_execute(db, ANALYZER_CREATE_INTERFACE, ANALYZER_GROUP_SCHEMA,
                       "Named", &statement) == EXECUTOR_OK,
              "CREATE INTERFACE succeeds");

        statement                     = (struct analyzer_statement){0};
        statement.fields.items        = base_fields;
        statement.fields.count        = 1;
        statement.fields.capacity     = 1;
        statement.implements.items    = named;
        statement.implements.count    = 1;
        statement.implements.capacity = 1;
        CHECK(_execute(db, ANALYZER_CREATE_CLASS, ANALYZER_GROUP_SCHEMA, "Base",
                       &statement) == EXECUTOR_OK,
              "CREATE CLASS satisfies implemented interface");

        statement                     = (struct analyzer_statement){0};
        statement.fields.items        = duplicate_fields;
        statement.fields.count        = 1;
        statement.fields.capacity     = 1;
        statement.implements.items    = named;
        statement.implements.count    = 1;
        statement.implements.capacity = 1;
        CHECK(
            _execute(db, ANALYZER_CREATE_CLASS, ANALYZER_GROUP_SCHEMA,
                     "BadContract", &statement) == EXECUTOR_ERROR_BAD_STATEMENT,
            "CREATE CLASS rejects an interface type mismatch");

        statement                 = (struct analyzer_statement){0};
        statement.fields.items    = duplicate_fields;
        statement.fields.count    = 1;
        statement.fields.capacity = 1;
        CHECK(_execute(db, ANALYZER_CREATE_CLASS, ANALYZER_GROUP_SCHEMA,
                       "BadContract", &statement) == EXECUTOR_OK,
              "create class used for schema dependency checks");

        statement                 = (struct analyzer_statement){0};
        statement.fields.items    = child_fields;
        statement.fields.count    = 1;
        statement.fields.capacity = 1;
        CHECK(_execute(db, ANALYZER_CREATE_CLASS, ANALYZER_GROUP_SCHEMA,
                       "Child", &statement) == EXECUTOR_OK,
              "CREATE CLASS for inheritance succeeds");
        statement        = (struct analyzer_statement){0};
        statement.parent = "Base";
        CHECK(_execute(db, ANALYZER_ALTER_CLASS_EXTENDS, ANALYZER_GROUP_SCHEMA,
                       "Child", &statement) == EXECUTOR_OK,
              "ALTER CLASS EXTENDS succeeds");
        statement                     = (struct analyzer_statement){0};
        statement.implements.items    = named;
        statement.implements.count    = 1;
        statement.implements.capacity = 1;
        CHECK(
            _execute(db, ANALYZER_ALTER_CLASS_IMPLEMENTS, ANALYZER_GROUP_SCHEMA,
                     "Child", &statement) == EXECUTOR_OK,
            "ALTER CLASS IMPLEMENTS accepts inherited interface fields");

        statement              = (struct analyzer_statement){0};
        statement.fields.items = (struct analyzer_field[]){
            {.name = "years", .type = {.kind = ANALYZER_TYPE_INTEGER}},
        };
        statement.fields.count    = 1;
        statement.fields.capacity = 1;
        CHECK(
            _execute(db, ANALYZER_ALTER_CLASS_ADD_FIELD, ANALYZER_GROUP_SCHEMA,
                     "Child", &statement) == EXECUTOR_OK,
            "ALTER CLASS ADD FIELD succeeds");
        statement          = (struct analyzer_statement){0};
        statement.field    = "years";
        statement.new_name = "age";
        CHECK(
            _execute(db, ANALYZER_ALTER_CLASS_RENAME_FIELD,
                     ANALYZER_GROUP_SCHEMA, "Child", &statement) == EXECUTOR_OK,
            "ALTER CLASS RENAME FIELD succeeds");
        statement       = (struct analyzer_statement){0};
        statement.field = "age";
        CHECK(
            _execute(db, ANALYZER_ALTER_CLASS_DROP_FIELD, ANALYZER_GROUP_SCHEMA,
                     "Child", &statement) == EXECUTOR_OK,
            "ALTER CLASS DROP FIELD succeeds");
        statement          = (struct analyzer_statement){0};
        statement.new_name = "RenamedChild";
        CHECK(_execute(db, ANALYZER_ALTER_CLASS_RENAME, ANALYZER_GROUP_SCHEMA,
                       "Child", &statement) == EXECUTOR_OK,
              "ALTER CLASS RENAME succeeds");
        statement = (struct analyzer_statement){0};
        CHECK(_execute(db, ANALYZER_DESCRIBE, ANALYZER_GROUP_METADATA,
                       "RenamedChild", &statement) == EXECUTOR_OK,
              "DESCRIBE reads the renamed class");

        statement = (struct analyzer_statement){0};
        CHECK(_execute(db, ANALYZER_DROP_INTERFACE, ANALYZER_GROUP_SCHEMA,
                       "Named", &statement) == EXECUTOR_ERROR_BAD_STATEMENT,
              "DROP INTERFACE is restricted while implemented");
        statement = (struct analyzer_statement){0};
        CHECK(_execute(db, ANALYZER_DROP_CLASS, ANALYZER_GROUP_SCHEMA, "Base",
                       &statement) == EXECUTOR_ERROR_BAD_STATEMENT,
              "DROP CLASS is restricted while inherited");

        statement              = (struct analyzer_statement){0};
        statement.function     = "raise";
        statement.fields.items = (struct analyzer_field[]){
            {.name = "percent", .type = {.kind = ANALYZER_TYPE_FLOAT}},
        };
        statement.fields.count     = 1;
        statement.fields.capacity  = 1;
        statement.return_type.kind = ANALYZER_TYPE_FLOAT;
        statement.expression       = (struct analyzer_expression[]){
            {
                .kind = ANALYZER_EXPRESSION_BINARY,
                .op   = ANALYZER_OPERATOR_MULTIPLY,
                .left =
                    &(struct analyzer_expression){
                        .kind  = ANALYZER_EXPRESSION_FIELD,
                        .field = "name",
                    },
                .right =
                    &(struct analyzer_expression){
                        .kind  = ANALYZER_EXPRESSION_FIELD,
                        .field = "percent",
                    },
            },
        };
        CHECK(_execute(db, ANALYZER_CREATE_FUNCTION, ANALYZER_GROUP_FUNCTION,
                       "RenamedChild",
                       &statement) == EXECUTOR_ERROR_BAD_STATEMENT,
              "CREATE FUNCTION rejects invalid expression types");

        statement = (struct analyzer_statement){0};
        CHECK(_execute(db, ANALYZER_DROP_CLASS, ANALYZER_GROUP_SCHEMA,
                       "RenamedChild", &statement) == EXECUTOR_OK,
              "DROP CLASS succeeds after dependents are removed");
        statement = (struct analyzer_statement){0};
        CHECK(_execute(db, ANALYZER_DROP_CLASS, ANALYZER_GROUP_SCHEMA, "Base",
                       &statement) == EXECUTOR_OK,
              "DROP CLASS succeeds after children are removed");
        statement = (struct analyzer_statement){0};
        CHECK(_execute(db, ANALYZER_DROP_CLASS, ANALYZER_GROUP_SCHEMA,
                       "BadContract", &statement) == EXECUTOR_OK,
              "DROP CLASS removes an unreferenced class");
        statement = (struct analyzer_statement){0};
        CHECK(_execute(db, ANALYZER_DROP_INTERFACE, ANALYZER_GROUP_SCHEMA,
                       "Named", &statement) == EXECUTOR_OK,
              "DROP INTERFACE succeeds after implementations are removed");
}

static void _schema_constraints(struct database* db) {
        struct analyzer_field parent_fields[] = {
            {.name = "value", .type = {.kind = ANALYZER_TYPE_INTEGER}},
        };
        struct analyzer_field child_fields[] = {
            {.name = "label", .type = {.kind = ANALYZER_TYPE_TEXT}},
        };
        struct analyzer_assignment assignments[] = {
            {.field = "value",
             .value = {.kind = ANALYZER_VALUE_INTEGER, .text = "1"}},
            {.field = "label",
             .value = {.kind = ANALYZER_VALUE_TEXT, .text = "child"}},
        };
        struct analyzer_statement statement = {0};

        statement.fields.items    = parent_fields;
        statement.fields.count    = 1;
        statement.fields.capacity = 1;
        CHECK(_execute(db, ANALYZER_CREATE_CLASS, ANALYZER_GROUP_SCHEMA,
                       "Parent", &statement) == EXECUTOR_OK,
              "create parent for schema constraint tests");

        statement                 = (struct analyzer_statement){0};
        statement.parent          = "Parent";
        statement.fields.items    = child_fields;
        statement.fields.count    = 1;
        statement.fields.capacity = 1;
        CHECK(_execute(db, ANALYZER_CREATE_CLASS, ANALYZER_GROUP_SCHEMA,
                       "Child", &statement) == EXECUTOR_OK,
              "create child with a separately declared field");

        statement                      = (struct analyzer_statement){0};
        statement.assignments.items    = assignments;
        statement.assignments.count    = 2;
        statement.assignments.capacity = 2;
        CHECK(_execute(db, ANALYZER_NEW, ANALYZER_GROUP_OBJECT, "Child",
                       &statement) == EXECUTOR_OK,
              "store an object in the descendant class");

        statement              = (struct analyzer_statement){0};
        statement.fields.items = (struct analyzer_field[]){
            {.name = "extra", .type = {.kind = ANALYZER_TYPE_TEXT}},
        };
        statement.fields.count    = 1;
        statement.fields.capacity = 1;
        CHECK(
            _execute(db, ANALYZER_ALTER_CLASS_ADD_FIELD, ANALYZER_GROUP_SCHEMA,
                     "Parent", &statement) == EXECUTOR_ERROR_BAD_STATEMENT,
            "field changes are rejected while a descendant has objects");

        statement = (struct analyzer_statement){0};
        CHECK(_execute(db, ANALYZER_DROP_CLASS, ANALYZER_GROUP_SCHEMA, "Parent",
                       &statement) == EXECUTOR_ERROR_BAD_STATEMENT,
              "DROP CLASS rejects a class with a subclass");
        CHECK(_execute(db, ANALYZER_DROP_CLASS, ANALYZER_GROUP_SCHEMA, "Child",
                       &statement) == EXECUTOR_ERROR_BAD_STATEMENT,
              "DROP CLASS rejects a class that still has objects");
        CHECK(i_execute(db,
                        &(struct analyzer_statement){
                            .kind  = ANALYZER_DELETE,
                            .group = ANALYZER_GROUP_OBJECT,
                            .name  = "Child",
                            .id    = "1",
                        }) == EXECUTOR_OK,
              "DELETE removes the descendant object");
        statement              = (struct analyzer_statement){0};
        statement.fields.items = (struct analyzer_field[]){
            {.name = "label", .type = {.kind = ANALYZER_TYPE_TEXT}},
        };
        statement.fields.count    = 1;
        statement.fields.capacity = 1;
        CHECK(
            _execute(db, ANALYZER_ALTER_CLASS_ADD_FIELD, ANALYZER_GROUP_SCHEMA,
                     "Parent", &statement) == EXECUTOR_ERROR_BAD_STATEMENT,
            "adding a field declared by a descendant is rejected");
        statement          = (struct analyzer_statement){0};
        statement.field    = "value";
        statement.new_name = "label";
        CHECK(_execute(db, ANALYZER_ALTER_CLASS_RENAME_FIELD,
                       ANALYZER_GROUP_SCHEMA, "Parent",
                       &statement) == EXECUTOR_ERROR_BAD_STATEMENT,
              "renaming a field to a descendant field name is rejected");
        CHECK(i_execute(db,
                        &(struct analyzer_statement){
                            .kind  = ANALYZER_DROP_CLASS,
                            .group = ANALYZER_GROUP_SCHEMA,
                            .name  = "Child",
                        }) == EXECUTOR_OK,
              "DROP CLASS succeeds after deleting the descendant object");
        CHECK(i_execute(db,
                        &(struct analyzer_statement){
                            .kind  = ANALYZER_DROP_CLASS,
                            .group = ANALYZER_GROUP_SCHEMA,
                            .name  = "Parent",
                        }) == EXECUTOR_OK,
              "DROP CLASS succeeds after removing its subclass");
}

static void _object_and_function_commands(struct database* db) {
        struct analyzer_field target_fields[] = {
            {.name = "label", .type = {.kind = ANALYZER_TYPE_TEXT}},
        };
        struct analyzer_field item_fields[] = {
            {.name = "rank", .type = {.kind = ANALYZER_TYPE_INTEGER}},
            {.name = "price", .type = {.kind = ANALYZER_TYPE_FLOAT}},
            {.name = "active", .type = {.kind = ANALYZER_TYPE_BOOL}},
            {.name = "title", .type = {.kind = ANALYZER_TYPE_TEXT}},
            {.name = "target",
             .type = {.kind = ANALYZER_TYPE_POINTER, .class_name = "Target"}},
        };
        struct analyzer_statement statement             = {0};
        struct analyzer_assignment target_assignments[] = {
            {.field = "label",
             .value = {.kind = ANALYZER_VALUE_TEXT, .text = "anchor"}},
        };
        struct analyzer_assignment item_assignments[] = {
            {.field = "rank",
             .value = {.kind = ANALYZER_VALUE_INTEGER, .text = "7"}},
            {.field = "price",
             .value = {.kind = ANALYZER_VALUE_FLOAT, .text = "2.5"}},
            {.field = "active",
             .value = {.kind = ANALYZER_VALUE_BOOL, .text = "true"}},
            {.field = "title",
             .value = {.kind = ANALYZER_VALUE_TEXT, .text = "Book"}},
            {.field = "target",
             .value = {.kind       = ANALYZER_VALUE_POINTER,
                       .text       = "1",
                       .class_name = "Target"}},
        };
        struct analyzer_assignment updates[] = {
            {.field = "rank",
             .value = {.kind = ANALYZER_VALUE_INTEGER, .text = "-4"}},
            {.field = "price",
             .value = {.kind = ANALYZER_VALUE_FLOAT, .text = "3.5"}},
            {.field = "title",
             .value = {.kind = ANALYZER_VALUE_TEXT, .text = "Longer book"}},
        };
        struct analyzer_field function_parameters[] = {
            {.name = "factor", .type = {.kind = ANALYZER_TYPE_FLOAT}},
        };
        struct analyzer_value call_values[] = {
            {.kind = ANALYZER_VALUE_INTEGER, .text = "2"},
        };
        struct analyzer_expression comparison_right = {
            .kind  = ANALYZER_EXPRESSION_LITERAL,
            .value = {.kind = ANALYZER_VALUE_INTEGER, .text = "-4"},
        };
        struct analyzer_expression comparison_left = {
            .kind  = ANALYZER_EXPRESSION_FIELD,
            .field = "rank",
        };
        struct analyzer_expression condition = {
            .kind  = ANALYZER_EXPRESSION_BINARY,
            .op    = ANALYZER_OPERATOR_GREATER_EQUAL,
            .left  = &comparison_left,
            .right = &comparison_right,
        };
        struct analyzer_expression price_field = {
            .kind  = ANALYZER_EXPRESSION_FIELD,
            .field = "price",
        };
        struct analyzer_expression factor_field = {
            .kind  = ANALYZER_EXPRESSION_FIELD,
            .field = "factor",
        };
        struct analyzer_expression function_expression = {
            .kind  = ANALYZER_EXPRESSION_BINARY,
            .op    = ANALYZER_OPERATOR_MULTIPLY,
            .left  = &price_field,
            .right = &factor_field,
        };
        size_t file_size = 0;
        uint8_t* bytes;
        uint64_t class_table_offset;
        uint64_t initial_class_table_offset = 0;
        uint64_t item_data_offset;
        uint64_t item_data_length;
        uint32_t item_record_length;
        size_t cursor;

        statement.fields.items    = target_fields;
        statement.fields.count    = 1;
        statement.fields.capacity = 1;
        CHECK(_execute(db, ANALYZER_CREATE_CLASS, ANALYZER_GROUP_SCHEMA,
                       "Target", &statement) == EXECUTOR_OK,
              "create pointer target class");
        statement                 = (struct analyzer_statement){0};
        statement.fields.items    = item_fields;
        statement.fields.count    = 5;
        statement.fields.capacity = 5;
        CHECK(_execute(db, ANALYZER_CREATE_CLASS, ANALYZER_GROUP_SCHEMA, "Item",
                       &statement) == EXECUTOR_OK,
              "create typed object class");

        bytes = _read_file(&file_size);
        CHECK(bytes != NULL, "read class table before object writes");
        if (bytes != NULL) {
                initial_class_table_offset = _u64(bytes + 32);
                free(bytes);
        }

        statement                      = (struct analyzer_statement){0};
        statement.assignments.items    = target_assignments;
        statement.assignments.count    = 1;
        statement.assignments.capacity = 1;
        CHECK(_execute(db, ANALYZER_NEW, ANALYZER_GROUP_OBJECT, "Target",
                       &statement) == EXECUTOR_OK,
              "NEW creates pointer target");
        statement                      = (struct analyzer_statement){0};
        statement.assignments.items    = item_assignments;
        statement.assignments.count    = 5;
        statement.assignments.capacity = 5;
        CHECK(_execute(db, ANALYZER_NEW, ANALYZER_GROUP_OBJECT, "Item",
                       &statement) == EXECUTOR_OK,
              "NEW stores integer, float, bool, text, and pointer values");

        statement                      = (struct analyzer_statement){0};
        statement.id                   = "1";
        statement.assignments.items    = updates;
        statement.assignments.count    = 3;
        statement.assignments.capacity = 3;
        CHECK(_execute(db, ANALYZER_SET, ANALYZER_GROUP_OBJECT, "Item",
                       &statement) == EXECUTOR_OK,
              "SET updates object values and a variable-length text field");

        statement            = (struct analyzer_statement){0};
        statement.expression = &condition;
        CHECK(_execute(db, ANALYZER_FIND, ANALYZER_GROUP_OBJECT, "Item",
                       &statement) == EXECUTOR_OK,
              "FIND evaluates a WHERE expression");
        statement = (struct analyzer_statement){0};
        CHECK(_execute(db, ANALYZER_FIND, ANALYZER_GROUP_OBJECT, "Item",
                       &statement) == EXECUTOR_OK,
              "FIND without WHERE succeeds");

        statement                  = (struct analyzer_statement){0};
        statement.function         = "total";
        statement.fields.items     = function_parameters;
        statement.fields.count     = 1;
        statement.fields.capacity  = 1;
        statement.return_type.kind = ANALYZER_TYPE_FLOAT;
        statement.expression       = &function_expression;
        CHECK(_execute(db, ANALYZER_CREATE_FUNCTION, ANALYZER_GROUP_FUNCTION,
                       "Item", &statement) == EXECUTOR_OK,
              "CREATE FUNCTION stores typed arithmetic expression");
        statement                 = (struct analyzer_statement){0};
        statement.function        = "total";
        statement.id              = "1";
        statement.values.items    = call_values;
        statement.values.count    = 1;
        statement.values.capacity = 1;
        CHECK(_execute(db, ANALYZER_CALL, ANALYZER_GROUP_FUNCTION, "Item",
                       &statement) == EXECUTOR_OK,
              "CALL evaluates expression with object and parameter fields");

        bytes = _read_file(&file_size);
        CHECK(bytes != NULL, "read object records from the database file");
        if (bytes != NULL) {
                class_table_offset = _u64(bytes + 32);
                item_data_offset   = _u64(bytes + class_table_offset + 48 + 16);
                item_data_length   = _u64(bytes + class_table_offset + 48 + 24);
                item_record_length =
                    item_data_offset <= file_size &&
                            file_size - (size_t)item_data_offset >= 12
                        ? _u32(bytes + item_data_offset + 8)
                        : 0;
                CHECK(_u64(bytes + class_table_offset + 48 + 32) == 1,
                      "class table object count matches stored record");
                CHECK(class_table_offset == initial_class_table_offset,
                      "object writes update the existing class table");
                CHECK(item_data_length > 0 && item_data_offset <= file_size &&
                          file_size - (size_t)item_data_offset >= 12 &&
                          item_record_length >= 16 &&
                          item_record_length <=
                              file_size - (size_t)item_data_offset - 12,
                      "object log head and record range are persisted");
                if (item_record_length >= 16 && item_data_offset <= file_size &&
                    file_size - (size_t)item_data_offset >=
                        12 + item_record_length) {
                        const uint8_t* record = bytes + item_data_offset + 12;
                        CHECK(_u64(record + 4) == 1 &&
                                  _u32(record) == item_record_length &&
                                  _u16(record + 14) == 5,
                              "object header stores ID, length, and field "
                              "count");
                        cursor = 16;
                        CHECK(_u32(record + cursor) != 0 &&
                                  record[cursor + 4] == 1 &&
                                  _u32(record + cursor + 5) == 8,
                              "integer field stores ID, type, and length");
                        cursor += 17;
                        CHECK(_u32(record + cursor) != 0 &&
                                  record[cursor + 4] == 2 &&
                                  _u32(record + cursor + 5) == 8,
                              "float field stores ID, type, and length");
                        cursor += 17;
                        CHECK(_u32(record + cursor) != 0 &&
                                  record[cursor + 4] == 4 &&
                                  _u32(record + cursor + 5) == 1 &&
                                  record[cursor + 9] == 1,
                              "boolean field stores its ID and one-byte value");
                        cursor += 10;
                        CHECK(_u32(record + cursor) != 0 &&
                                  record[cursor + 4] == 3 &&
                                  _u32(record + cursor + 5) ==
                                      strlen("Longer book") &&
                                  memcmp(record + cursor + 9, "Longer book",
                                         strlen("Longer book")) == 0,
                              "SET persists resized text bytes");
                        cursor += 9 + strlen("Longer book");
                        CHECK(_u32(record + cursor) != 0 &&
                                  record[cursor + 4] == 5 &&
                                  _u32(record + cursor + 5) == 12 &&
                                  _u32(record + cursor + 9) ==
                                      _u32(bytes + class_table_offset) &&
                                  _u64(record + cursor + 13) == 1,
                              "pointer field stores class ID and object ID");
                }
                free(bytes);
        }

        statement      = (struct analyzer_statement){0};
        statement.name = "Target";
        statement.id   = "1";
        CHECK(i_execute(db,
                        &(struct analyzer_statement){
                            .kind  = ANALYZER_DELETE,
                            .group = ANALYZER_GROUP_OBJECT,
                            .name  = "Target",
                            .id    = "1",
                        }) == EXECUTOR_ERROR_BAD_STATEMENT,
              "DELETE rejects an object referenced by a pointer");
        CHECK(i_execute(db,
                        &(struct analyzer_statement){
                            .kind  = ANALYZER_DELETE,
                            .group = ANALYZER_GROUP_OBJECT,
                            .name  = "Item",
                            .id    = "1",
                        }) == EXECUTOR_OK,
              "DELETE removes an unreferenced object");
        CHECK(i_execute(db,
                        &(struct analyzer_statement){
                            .kind  = ANALYZER_DELETE,
                            .group = ANALYZER_GROUP_OBJECT,
                            .name  = "Target",
                            .id    = "1",
                        }) == EXECUTOR_OK,
              "DELETE succeeds after pointer owner is removed");
        CHECK(i_execute(db,
                        &(struct analyzer_statement){
                            .kind  = ANALYZER_DROP_CLASS,
                            .group = ANALYZER_GROUP_SCHEMA,
                            .name  = "Target",
                        }) == EXECUTOR_ERROR_BAD_STATEMENT,
              "DROP CLASS rejects a target used by a pointer field");
        bytes = _read_file(&file_size);
        CHECK(bytes != NULL, "read database after object deletions");
        if (bytes != NULL) {
                size_t class_count = _u16(bytes + 10);
                size_t class_index;
                class_table_offset = _u64(bytes + 32);
                CHECK(class_count == 2,
                      "class table retains both classes after record deletion");
                for (class_index = 0; class_index < class_count;
                     class_index++) {
                        const uint8_t* class_entry =
                            bytes + class_table_offset + class_index * 48;
                        CHECK(_u64(class_entry + 32) == 0 &&
                                  _u64(class_entry + 24) > 0,
                              "DELETE appends tombstone records and clears "
                              "object count");
                        CHECK(_u64(class_entry + 40) == 2,
                              "DELETE preserves the next object ID");
                }
                free(bytes);
        }
        CHECK(i_execute(db,
                        &(struct analyzer_statement){
                            .kind  = ANALYZER_DROP_CLASS,
                            .group = ANALYZER_GROUP_SCHEMA,
                            .name  = "Item",
                        }) == EXECUTOR_ERROR_BAD_STATEMENT,
              "DROP CLASS rejects a class that still owns a function");
        CHECK(i_execute(db,
                        &(struct analyzer_statement){
                            .kind     = ANALYZER_DROP_FUNCTION,
                            .group    = ANALYZER_GROUP_FUNCTION,
                            .name     = "Item",
                            .function = "total",
                        }) == EXECUTOR_OK,
              "DROP FUNCTION removes a stored function");
        CHECK(i_execute(db,
                        &(struct analyzer_statement){
                            .kind  = ANALYZER_DROP_CLASS,
                            .group = ANALYZER_GROUP_SCHEMA,
                            .name  = "Item",
                        }) == EXECUTOR_OK,
              "DROP CLASS removes object class after records are deleted");
        CHECK(i_execute(db,
                        &(struct analyzer_statement){
                            .kind  = ANALYZER_DROP_CLASS,
                            .group = ANALYZER_GROUP_SCHEMA,
                            .name  = "Target",
                        }) == EXECUTOR_OK,
              "DROP CLASS removes pointer target after field owner is removed");
}

int main(void) {
        struct database db;
        struct analyzer_field field = {
            .name = "value",
            .type = {.kind = ANALYZER_TYPE_INTEGER},
        };
        struct analyzer_statement statement = {0};

        remove(TEST_FILE);
        CHECK(database_init_format(&db, TEST_FILE, true),
              "initialize README-format executor test database");
        _schema_and_function_commands(&db);
        _schema_constraints(&db);
        _object_and_function_commands(&db);
        database_close(&db);
        _test_legacy_migration();
        CHECK(database_init_format_mode(&db, TEST_FILE, false, true),
              "open existing database in read-only mode");
        statement.kind  = ANALYZER_LIST_CLASSES;
        statement.group = ANALYZER_GROUP_METADATA;
        CHECK(i_execute(&db, &statement) == EXECUTOR_OK,
              "read-only database supports metadata reads");
        statement                 = (struct analyzer_statement){0};
        statement.kind            = ANALYZER_CREATE_CLASS;
        statement.group           = ANALYZER_GROUP_SCHEMA;
        statement.name            = "ReadOnlyWrite";
        statement.fields.items    = &field;
        statement.fields.count    = 1;
        statement.fields.capacity = 1;
        CHECK(i_execute(&db, &statement) != EXECUTOR_OK,
              "read-only database rejects mutations");
        database_close(&db);
        remove(TEST_FILE);
        printf("executor command tests:\n");
        printf("%d checks, %d failures\n", checks, failures);
        return failures == 0 ? 0 : 1;
}
