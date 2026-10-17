/**
 * @file create_interface.c
 * @brief Check CREATE INTERFACE output against the binary layout in README.md.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interface/executor/executor.h"
#include "interface/storage/database.h"

#define TEST_FILE "build/create-interface-test.oopdb"
#define INTERFACE_ENTRY_SIZE 16u

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

static void _test_create_interface_bytes(void) {
        struct database db;
        struct analyzer_field fields[] = {
            {.name = "name", .type = {.kind = ANALYZER_TYPE_TEXT}},
            {.name = "active", .type = {.kind = ANALYZER_TYPE_BOOL}},
        };
        struct analyzer_statement statement = {
            .kind   = ANALYZER_CREATE_INTERFACE,
            .group  = ANALYZER_GROUP_SCHEMA,
            .name   = "UserInterface",
            .fields = {.items = fields, .count = 2, .capacity = 2},
        };
        uint8_t* bytes;
        size_t file_size = 0;
        uint64_t table_offset;
        uint64_t table_length;
        const uint8_t* entry;
        uint64_t definition_offset;
        uint32_t definition_length;
        size_t offset;
        size_t definition_end;
        uint16_t name_length;
        uint16_t field_name_length;
        uint32_t first_field_id;
        uint32_t second_field_id;

        remove(TEST_FILE);
        CHECK(database_init_format(&db, TEST_FILE, true),
              "create README-format database");
        CHECK(i_execute(&db, &statement) == EXECUTOR_OK,
              "CREATE INTERFACE command succeeds");
        database_close(&db);

        bytes = _read_file(&file_size);
        CHECK(bytes != NULL, "database file is read directly as bytes");
        if (bytes == NULL) {
                remove(TEST_FILE);
                return;
        }

        CHECK(file_size >= 64 && memcmp(bytes, "OOPDB\0\0\0", 8) == 0,
              "file starts with the 8-byte database magic");
        if (file_size < 64 || memcmp(bytes, "OOPDB\0\0\0", 8) != 0) {
                free(bytes);
                remove(TEST_FILE);
                return;
        }
        CHECK(_u16(bytes + 8) == 1 && _u16(bytes + 10) == 0 &&
                  _u16(bytes + 12) == 0 && _u16(bytes + 14) == 0,
              "16-byte header stores interface, class, function, and reserved "
              "counts");

        table_offset = _u64(bytes + 16);
        table_length = _u64(bytes + 24);
        CHECK(table_length == INTERFACE_ENTRY_SIZE && _u64(bytes + 32) == 0 &&
                  _u64(bytes + 40) == 0 && _u64(bytes + 48) == 0 &&
                  _u64(bytes + 56) == 0,
              "48-byte metatable points to one interface table entry");
        if (table_length != INTERFACE_ENTRY_SIZE || table_offset < 64 ||
            !_range(file_size, table_offset, table_length)) {
                free(bytes);
                remove(TEST_FILE);
                return;
        }

        entry             = bytes + table_offset;
        definition_offset = _u64(entry + 4);
        definition_length = _u32(entry + 12);
        CHECK(_u32(entry) != 0,
              "interface table entry has a stable nonzero interface ID");
        CHECK(
            definition_length >= 4 &&
                definition_offset >= table_offset + table_length &&
                _range(file_size, definition_offset, definition_length),
            "interface definition follows the table and fits inside the file");
        if (definition_length < 4 ||
            definition_offset < table_offset + table_length ||
            !_range(file_size, definition_offset, definition_length)) {
                free(bytes);
                remove(TEST_FILE);
                return;
        }

        offset         = (size_t)definition_offset;
        definition_end = offset + definition_length;
        name_length    = _u16(bytes + offset);
        CHECK(_u16(bytes + offset + 2) == 2,
              "interface definition stores the required field count");
        offset += 4;
        CHECK(name_length == strlen("UserInterface") &&
                  name_length <= definition_end - offset &&
                  memcmp(bytes + offset, "UserInterface", name_length) == 0,
              "interface definition stores its length-prefixed name");
        if (name_length > definition_end - offset) {
                free(bytes);
                remove(TEST_FILE);
                return;
        }
        offset += name_length;

        if (offset > definition_end || definition_end - offset < 7) {
                free(bytes);
                remove(TEST_FILE);
                return;
        }
        first_field_id = _u32(bytes + offset);
        CHECK(first_field_id != 0 && bytes[offset + 4] == 3,
              "first field has a stable ID and text type code");
        field_name_length = _u16(bytes + offset + 5);
        offset += 7;
        CHECK(field_name_length == strlen("name") &&
                  field_name_length <= definition_end - offset &&
                  memcmp(bytes + offset, "name", field_name_length) == 0,
              "first field descriptor stores the name");
        if (field_name_length > definition_end - offset) {
                free(bytes);
                remove(TEST_FILE);
                return;
        }
        offset += field_name_length;

        if (offset > definition_end || definition_end - offset < 7) {
                free(bytes);
                remove(TEST_FILE);
                return;
        }
        second_field_id = _u32(bytes + offset);
        CHECK(second_field_id != 0 && second_field_id != first_field_id &&
                  bytes[offset + 4] == 4,
              "second field has a unique stable ID and bool type code");
        field_name_length = _u16(bytes + offset + 5);
        offset += 7;
        CHECK(field_name_length == strlen("active") &&
                  field_name_length <= definition_end - offset &&
                  memcmp(bytes + offset, "active", field_name_length) == 0,
              "second field descriptor stores the name");
        if (field_name_length > definition_end - offset) {
                free(bytes);
                remove(TEST_FILE);
                return;
        }
        offset += field_name_length;
        CHECK(offset == definition_end,
              "interface definition length ends after its field descriptors");

        free(bytes);
        remove(TEST_FILE);
}

int main(void) {
        _test_create_interface_bytes();
        printf("create_interface tests:\n");
        printf("%d checks, %d failures\n", checks, failures);
        return failures != 0;
}
