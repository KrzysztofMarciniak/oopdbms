/**
 * @file create_class_extends.c
 * @brief Check CREATE CLASS inheritance against the binary layout in README.md.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interface/executor/executor.h"
#include "interface/storage/database.h"

#define TEST_FILE "build/create-class-extends-test.oopdb"
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

static bool _check_class_definition(const uint8_t* bytes, size_t file_size,
                                    uint64_t offset, uint32_t length,
                                    const char* name, uint32_t expected_parent,
                                    uint32_t expected_field_id,
                                    uint8_t expected_type,
                                    const char* expected_field) {
        size_t cursor;
        size_t end;
        uint16_t name_length;
        uint16_t field_name_length;
        bool valid;

        if (length < 16 || !_range(file_size, offset, length)) {
                return false;
        }
        cursor      = (size_t)offset;
        end         = cursor + length;
        name_length = _u16(bytes + cursor);
        valid = _u16(bytes + cursor + 2) == 0 &&
                _u32(bytes + cursor + 4) == expected_parent &&
                _u32(bytes + cursor + 8) == 1 && _u32(bytes + cursor + 12) == 0;
        CHECK(valid,
              "definition stores flags, parent, one field, no interfaces");
        cursor += 16;
        if (name_length > end - cursor) {
                return false;
        }
        valid = name_length == strlen(name) &&
                memcmp(bytes + cursor, name, name_length) == 0;
        CHECK(valid, "definition stores its length-prefixed class name");
        cursor += name_length;
        if (end - cursor < 7) {
                return false;
        }
        valid = _u32(bytes + cursor) == expected_field_id &&
                bytes[cursor + 4] == expected_type;
        CHECK(valid, "definition stores its field ID and type code");
        field_name_length = _u16(bytes + cursor + 5);
        cursor += 7;
        if (field_name_length > end - cursor) {
                return false;
        }
        valid = field_name_length == strlen(expected_field) &&
                memcmp(bytes + cursor, expected_field, field_name_length) == 0;
        CHECK(valid, "definition stores its field name");
        cursor += field_name_length;
        valid = cursor == end;
        CHECK(valid, "definition ends after its directly declared field");
        return valid;
}

static void _test_create_class_extends(void) {
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
        struct analyzer_field child_fields[] = {
            {.name = "label", .type = {.kind = ANALYZER_TYPE_TEXT}},
        };
        struct analyzer_statement child = {
            .kind   = ANALYZER_CREATE_CLASS,
            .group  = ANALYZER_GROUP_SCHEMA,
            .name   = "Derived",
            .parent = "Base",
            .fields = {.items = child_fields, .count = 1, .capacity = 1},
        };
        uint8_t* bytes;
        size_t file_size = 0;
        uint64_t table_offset;
        uint64_t table_length;
        uint32_t base_id                   = 0;
        uint32_t derived_id                = 0;
        uint32_t derived_parent_id         = 0;
        uint32_t base_field_id             = 0;
        uint32_t derived_field_id          = 0;
        uint64_t derived_definition_offset = 0;
        uint32_t derived_definition_length = 0;
        bool found_base                    = false;
        bool found_derived                 = false;
        size_t i;

        remove(TEST_FILE);
        CHECK(database_init_format(&db, TEST_FILE, true),
              "create README-format database");
        CHECK(i_execute(&db, &base) == EXECUTOR_OK,
              "CREATE CLASS Base succeeds");
        CHECK(i_execute(&db, &child) == EXECUTOR_OK,
              "CREATE CLASS Derived EXTENDS Base succeeds");
        database_close(&db);

        bytes = _read_file(&file_size);
        CHECK(bytes != NULL, "database file is read directly as bytes");
        if (bytes == NULL) {
                remove(TEST_FILE);
                return;
        }
        CHECK(file_size >= 64 && memcmp(bytes, "OOPDB\0\0\0", 8) == 0,
              "file starts with the database header and magic");
        if (file_size < 64 || memcmp(bytes, "OOPDB\0\0\0", 8) != 0) {
                free(bytes);
                remove(TEST_FILE);
                return;
        }
        CHECK(_u16(bytes + 8) == 0 && _u16(bytes + 10) == 2 &&
                  _u16(bytes + 12) == 0 && _u16(bytes + 14) == 0,
              "header stores two classes and no interfaces or functions");

        table_offset = _u64(bytes + 32);
        table_length = _u64(bytes + 40);
        CHECK(_u64(bytes + 16) == 0 && _u64(bytes + 24) == 0 &&
                  table_length == 2 * CLASS_ENTRY_SIZE &&
                  _u64(bytes + 48) == 0 && _u64(bytes + 56) == 0,
              "metatable points to two class table entries");
        if (table_length != 2 * CLASS_ENTRY_SIZE || table_offset < 64 ||
            !_range(file_size, table_offset, table_length)) {
                free(bytes);
                remove(TEST_FILE);
                return;
        }

        for (i = 0; i < 2; i++) {
                const uint8_t* entry =
                    bytes + table_offset + i * CLASS_ENTRY_SIZE;
                uint32_t class_id          = _u32(entry);
                uint64_t definition_offset = _u64(entry + 4);
                uint32_t definition_length = _u32(entry + 12);
                uint16_t name_length;
                const uint8_t* definition;
                uint64_t name_offset;

                if (definition_length < 16 ||
                    !_range(file_size, definition_offset, definition_length)) {
                        CHECK(false, "class definition range is valid");
                        continue;
                }
                definition  = bytes + definition_offset;
                name_length = _u16(definition);
                name_offset = definition_offset + 16;
                if (name_length > definition_length - 16) {
                        CHECK(false, "class name fits its definition");
                        continue;
                }
                if (definition_length - 16 - name_length < 7) {
                        CHECK(false,
                              "class field descriptor fits its definition");
                        continue;
                }
                if (name_length == strlen("Base") &&
                    memcmp(bytes + name_offset, "Base", name_length) == 0) {
                        found_base    = true;
                        base_id       = class_id;
                        base_field_id = _u32(definition + 16 + name_length);
                        CHECK(class_id != 0,
                              "base class table entry has a stable ID");
                        _check_class_definition(bytes, file_size,
                                                definition_offset,
                                                definition_length, "Base", 0,
                                                base_field_id, 1, "id");
                } else if (name_length == strlen("Derived") &&
                           memcmp(bytes + name_offset, "Derived",
                                  name_length) == 0) {
                        found_derived     = true;
                        derived_id        = class_id;
                        derived_parent_id = _u32(definition + 4);
                        derived_field_id  = _u32(definition + 16 + name_length);
                        derived_definition_offset = definition_offset;
                        derived_definition_length = definition_length;
                        CHECK(class_id != 0,
                              "derived class table entry has a stable ID");
                } else {
                        CHECK(false, "class table entry names Base or Derived");
                }
        }
        CHECK(found_base && found_derived,
              "class table contains Base and Derived definitions");
        if (found_derived && found_base) {
                _check_class_definition(bytes, file_size,
                                        derived_definition_offset,
                                        derived_definition_length, "Derived",
                                        base_id, derived_field_id, 3, "label");
        }
        CHECK(base_id != 0 && derived_id != 0 && base_id != derived_id &&
                  derived_parent_id == base_id,
              "derived parent_class_id refers to the base class ID");
        CHECK(base_field_id != 0 && derived_field_id != 0 &&
                  base_field_id != derived_field_id,
              "field IDs are unique across both class definitions");

        free(bytes);
        remove(TEST_FILE);
}

int main(void) {
        _test_create_class_extends();
        printf("create_class_extends tests:\n");
        printf("%d checks, %d failures\n", checks, failures);
        return failures != 0;
}
