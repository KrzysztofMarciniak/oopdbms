/**
 * @file schema_mutations.c
 * @brief Check schema mutations against the binary layout in README.md.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interface/executor/executor.h"
#include "interface/storage/database.h"

#define TEST_FILE "build/schema-mutations-test.oopdb"
#define CLASS_ENTRY_SIZE 48u

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

static bool _range(size_t file_size, uint64_t offset, uint64_t length) {
        return offset <= file_size && length <= file_size - (size_t)offset;
}

static uint8_t* _read_file(size_t* file_size) {
        FILE* file = fopen(TEST_FILE, "rb");
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

static bool _check_class(const uint8_t* bytes, size_t file_size,
                         const char* expected_name,
                         const char* const* expected_fields,
                         const uint8_t* expected_types, size_t field_count,
                         uint32_t* class_id, uint32_t* field_ids) {
        uint64_t table_offset;
        uint64_t table_length;
        const uint8_t* entry;
        uint64_t definition_offset;
        uint32_t definition_length;
        const uint8_t* definition;
        size_t cursor;
        size_t definition_end;
        uint16_t name_length;
        uint32_t stored_field_count;
        size_t index;

        if (file_size < 64 || memcmp(bytes, "OOPDB\0\0\0", 8) != 0) {
                CHECK(false, "database header and magic are present");
                return false;
        }
        CHECK(_u16(bytes + 10) == 1,
              "header class count matches the remaining class");
        table_offset = _u64(bytes + 32);
        table_length = _u64(bytes + 40);
        CHECK(table_length == CLASS_ENTRY_SIZE &&
                  _range(file_size, table_offset, table_length),
              "metatable class range contains one in-file entry");
        if (table_length != CLASS_ENTRY_SIZE ||
            !_range(file_size, table_offset, table_length)) {
                return false;
        }

        entry             = bytes + table_offset;
        *class_id         = _u32(entry);
        definition_offset = _u64(entry + 4);
        definition_length = _u32(entry + 12);
        CHECK(*class_id != 0, "class table retains a nonzero stable ID");
        CHECK(definition_length >= 16 &&
                  _range(file_size, definition_offset, definition_length),
              "class definition range fits in the file");
        if (definition_length < 16 ||
            !_range(file_size, definition_offset, definition_length)) {
                return false;
        }

        definition         = bytes + definition_offset;
        definition_end     = (size_t)definition_offset + definition_length;
        name_length        = _u16(definition);
        stored_field_count = _u32(definition + 8);
        CHECK(stored_field_count == field_count,
              "class definition stores the expected field count");
        cursor = (size_t)definition_offset + 16;
        CHECK(name_length <= definition_end - cursor &&
                  name_length == strlen(expected_name) &&
                  memcmp(bytes + cursor, expected_name, name_length) == 0,
              "class definition stores the current length-prefixed name");
        if (name_length > definition_end - cursor) {
                return false;
        }
        cursor += name_length;

        for (index = 0; index < field_count; index++) {
                uint16_t field_name_length;

                if (cursor > definition_end || definition_end - cursor < 7) {
                        CHECK(false, "field descriptor fits its definition");
                        return false;
                }
                field_ids[index]  = _u32(bytes + cursor);
                field_name_length = _u16(bytes + cursor + 5);
                CHECK(field_ids[index] != 0 &&
                          bytes[cursor + 4] == expected_types[index],
                      "field retains a nonzero ID and expected type");
                cursor += 7;
                CHECK(field_name_length <= definition_end - cursor &&
                          field_name_length == strlen(expected_fields[index]) &&
                          memcmp(bytes + cursor, expected_fields[index],
                                 field_name_length) == 0,
                      "field descriptor stores the expected name");
                if (field_name_length > definition_end - cursor) {
                        return false;
                }
                cursor += field_name_length;
        }
        CHECK(cursor == definition_end,
              "class definition ends after its field descriptors");
        return cursor == definition_end;
}

static void _test_mutations_preserve_ids_and_ranges(void) {
        struct database db;
        struct analyzer_field initial_fields[] = {
            {.name = "name", .type = {.kind = ANALYZER_TYPE_TEXT}},
        };
        struct analyzer_field added_field = {
            .name = "city",
            .type = {.kind = ANALYZER_TYPE_TEXT},
        };
        struct analyzer_field temporary_field = {
            .name = "value",
            .type = {.kind = ANALYZER_TYPE_INTEGER},
        };
        struct analyzer_statement statement = {
            .kind   = ANALYZER_CREATE_CLASS,
            .group  = ANALYZER_GROUP_SCHEMA,
            .name   = "Person",
            .fields = {.items = initial_fields, .count = 1, .capacity = 1},
        };
        struct analyzer_statement add_field = {
            .kind   = ANALYZER_ALTER_CLASS_ADD_FIELD,
            .group  = ANALYZER_GROUP_SCHEMA,
            .name   = "Person",
            .fields = {.items = &added_field, .count = 1, .capacity = 1},
        };
        struct analyzer_statement rename_class = {
            .kind     = ANALYZER_ALTER_CLASS_RENAME,
            .group    = ANALYZER_GROUP_SCHEMA,
            .name     = "Person",
            .new_name = "Human",
        };
        struct analyzer_statement rename_field = {
            .kind     = ANALYZER_ALTER_CLASS_RENAME_FIELD,
            .group    = ANALYZER_GROUP_SCHEMA,
            .name     = "Human",
            .field    = "city",
            .new_name = "town",
        };
        struct analyzer_statement temporary_class = {
            .kind   = ANALYZER_CREATE_CLASS,
            .group  = ANALYZER_GROUP_SCHEMA,
            .name   = "Temporary",
            .fields = {.items = &temporary_field, .count = 1, .capacity = 1},
        };
        struct analyzer_statement drop_temporary = {
            .kind  = ANALYZER_DROP_CLASS,
            .group = ANALYZER_GROUP_SCHEMA,
            .name  = "Temporary",
        };
        const char* original_names[]   = {"name"};
        const uint8_t text_type[]      = {3};
        const char* added_names[]      = {"name", "city"};
        const uint8_t added_types[]    = {3, 3};
        const char* renamed_names[]    = {"name", "town"};
        uint32_t original_class_id     = 0;
        uint32_t original_field_ids[2] = {0, 0};
        uint32_t middle_class_id       = 0;
        uint32_t middle_field_ids[2]   = {0, 0};
        uint32_t final_class_id        = 0;
        uint32_t final_field_ids[2]    = {0, 0};
        size_t file_size               = 0;
        uint8_t* bytes;

        remove(TEST_FILE);
        CHECK(database_init_format(&db, TEST_FILE, true),
              "create README-format database");
        CHECK(i_execute(&db, &statement) == EXECUTOR_OK,
              "CREATE CLASS Person succeeds");
        database_close(&db);

        bytes = _read_file(&file_size);
        CHECK(bytes != NULL, "read initial schema bytes directly");
        if (bytes == NULL) {
                remove(TEST_FILE);
                return;
        }
        _check_class(bytes, file_size, "Person", original_names, text_type, 1,
                     &original_class_id, original_field_ids);
        free(bytes);

        CHECK(database_init_format(&db, TEST_FILE, false),
              "reopen database before schema mutations");
        CHECK(i_execute(&db, &add_field) == EXECUTOR_OK,
              "ALTER CLASS ADD FIELD succeeds");
        CHECK(i_execute(&db, &rename_class) == EXECUTOR_OK,
              "ALTER CLASS RENAME succeeds");
        database_close(&db);

        bytes = _read_file(&file_size);
        CHECK(bytes != NULL, "read bytes after adding and renaming class");
        if (bytes == NULL) {
                remove(TEST_FILE);
                return;
        }
        _check_class(bytes, file_size, "Human", added_names, added_types, 2,
                     &middle_class_id, middle_field_ids);
        CHECK(middle_class_id == original_class_id &&
                  middle_field_ids[0] == original_field_ids[0],
              "class rename and field addition preserve existing IDs");
        free(bytes);

        CHECK(database_init_format(&db, TEST_FILE, false),
              "reopen database before field and class deletion");
        CHECK(i_execute(&db, &rename_field) == EXECUTOR_OK,
              "ALTER CLASS RENAME FIELD succeeds");
        CHECK(i_execute(&db, &temporary_class) == EXECUTOR_OK,
              "create a second class before deletion");
        CHECK(i_execute(&db, &drop_temporary) == EXECUTOR_OK,
              "DROP CLASS removes the second class");
        database_close(&db);

        bytes = _read_file(&file_size);
        CHECK(bytes != NULL, "read bytes after field rename and class drop");
        if (bytes != NULL) {
                _check_class(bytes, file_size, "Human", renamed_names,
                             added_types, 2, &final_class_id, final_field_ids);
                CHECK(final_class_id == original_class_id &&
                          final_field_ids[0] == original_field_ids[0] &&
                          final_field_ids[1] == middle_field_ids[1],
                      "class and field renames preserve stable IDs");
                free(bytes);
        }

        remove(TEST_FILE);
}

int main(void) {
        _test_mutations_preserve_ids_and_ranges();
        printf("schema_mutations tests:\n");
        printf("%d checks, %d failures\n", checks, failures);
        return failures != 0;
}
