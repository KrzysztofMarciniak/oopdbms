/**
 * @file create_class.c
 * @brief Check CREATE CLASS output against the binary layout in README.md.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interface/executor/executor.h"
#include "interface/storage/database.h"

#define TEST_FILE "build/create-class-test.oopdb"
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

static void _test_create_class_bytes(void) {
        struct database db;
        struct analyzer_field fields[] = {
            {.name = "name", .type = {.kind = ANALYZER_TYPE_TEXT}},
            {.name = "age", .type = {.kind = ANALYZER_TYPE_INTEGER}},
        };
        struct analyzer_statement statement = {
            .kind   = ANALYZER_CREATE_CLASS,
            .group  = ANALYZER_GROUP_SCHEMA,
            .name   = "Person",
            .fields = {.items = fields, .count = 2, .capacity = 2},
        };
        uint8_t* bytes;
        size_t file_size = 0;
        uint64_t class_table_offset;
        uint64_t class_table_length;
        const uint8_t* class_entry;
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
              "CREATE CLASS command succeeds");
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
        CHECK(_u16(bytes + 8) == 0 && _u16(bytes + 10) == 1 &&
                  _u16(bytes + 12) == 0 && _u16(bytes + 14) == 0,
              "16-byte header stores interface, class, function, and reserved "
              "counts");

        class_table_offset = _u64(bytes + 32);
        class_table_length = _u64(bytes + 40);
        CHECK(_u64(bytes + 16) == 0 && _u64(bytes + 24) == 0 &&
                  class_table_length == CLASS_ENTRY_SIZE &&
                  _u64(bytes + 48) == 0 && _u64(bytes + 56) == 0,
              "48-byte metatable points to one class table entry");
        if (class_table_length != CLASS_ENTRY_SIZE || class_table_offset < 64 ||
            !_range(file_size, class_table_offset, class_table_length)) {
                free(bytes);
                remove(TEST_FILE);
                return;
        }

        class_entry       = bytes + class_table_offset;
        definition_offset = _u64(class_entry + 4);
        definition_length = _u32(class_entry + 12);
        CHECK(_u32(class_entry) != 0,
              "class table entry has a stable nonzero class ID");
        CHECK(_u64(class_entry + 24) == 0 && _u64(class_entry + 32) == 0,
              "new class has an empty data segment and zero objects");
        CHECK(
            definition_length >= 16 &&
                definition_offset >= class_table_offset + class_table_length &&
                _range(file_size, definition_offset, definition_length),
            "class definition follows the table and fits inside the file");
        if (definition_length < 16 ||
            !_range(file_size, definition_offset, definition_length)) {
                free(bytes);
                remove(TEST_FILE);
                return;
        }

        offset         = (size_t)definition_offset;
        definition_end = offset + definition_length;
        name_length    = _u16(bytes + offset);
        CHECK(_u16(bytes + offset + 2) == 0 && _u32(bytes + offset + 4) == 0 &&
                  _u32(bytes + offset + 8) == 2 &&
                  _u32(bytes + offset + 12) == 0,
              "class definition stores flags, parent, field, and interface "
              "counts");
        offset += 16;
        CHECK(name_length == strlen("Person") &&
                  name_length <= definition_end - offset &&
                  memcmp(bytes + offset, "Person", name_length) == 0,
              "class definition stores its length-prefixed name");
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
                  bytes[offset + 4] == 1,
              "second field has a unique stable ID and integer type code");
        field_name_length = _u16(bytes + offset + 5);
        offset += 7;
        CHECK(field_name_length == strlen("age") &&
                  field_name_length <= definition_end - offset &&
                  memcmp(bytes + offset, "age", field_name_length) == 0,
              "second field descriptor stores the name");
        if (field_name_length > definition_end - offset) {
                free(bytes);
                remove(TEST_FILE);
                return;
        }
        offset += field_name_length;
        CHECK(offset == definition_end,
              "class definition length ends after its field descriptors");

        free(bytes);
        remove(TEST_FILE);
}

int main(void) {
        _test_create_class_bytes();
        printf("create_class tests:\n");
        printf("%d checks, %d failures\n", checks, failures);
        return failures != 0;
}
