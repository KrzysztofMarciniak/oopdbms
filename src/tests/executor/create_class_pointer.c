/**
 * @file create_class_pointer.c
 * @brief Check pointer field descriptors against README.md.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interface/executor/executor.h"
#include "interface/storage/database.h"

#define TEST_FILE "build/create-class-pointer-test.oopdb"
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

static void _test_pointer_field(void) {
        struct database db;
        struct analyzer_field person_fields[] = {
            {.name = "name", .type = {.kind = ANALYZER_TYPE_TEXT}},
        };
        struct analyzer_statement person = {
            .kind   = ANALYZER_CREATE_CLASS,
            .group  = ANALYZER_GROUP_SCHEMA,
            .name   = "Person",
            .fields = {.items = person_fields, .count = 1, .capacity = 1},
        };
        struct analyzer_field book_fields[] = {
            {.name = "author",
             .type = {.kind = ANALYZER_TYPE_POINTER, .class_name = "Person"}},
        };
        struct analyzer_statement book = {
            .kind   = ANALYZER_CREATE_CLASS,
            .group  = ANALYZER_GROUP_SCHEMA,
            .name   = "Book",
            .fields = {.items = book_fields, .count = 1, .capacity = 1},
        };
        uint8_t* bytes;
        size_t file_size = 0;
        uint64_t table_offset;
        uint64_t table_length;
        uint32_t person_id              = 0;
        uint64_t book_definition_offset = 0;
        uint32_t book_definition_length = 0;
        uint16_t book_name_length;
        size_t i;

        remove(TEST_FILE);
        CHECK(database_init_format(&db, TEST_FILE, true),
              "create README-format database");
        CHECK(i_execute(&db, &person) == EXECUTOR_OK,
              "CREATE CLASS Person succeeds");
        CHECK(i_execute(&db, &book) == EXECUTOR_OK,
              "CREATE CLASS Book with pointer field succeeds");
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
                const uint8_t* definition;

                if (definition_length < 16 ||
                    !_range(file_size, definition_offset, definition_length)) {
                        CHECK(false, "class definition range is valid");
                        continue;
                }
                definition       = bytes + definition_offset;
                book_name_length = _u16(definition);
                if (book_name_length > definition_length - 16) {
                        CHECK(false, "class name fits its definition");
                        continue;
                }
                if (book_name_length == strlen("Person") &&
                    memcmp(definition + 16, "Person", book_name_length) == 0) {
                        person_id = class_id;
                } else if (book_name_length == strlen("Book") &&
                           memcmp(definition + 16, "Book", book_name_length) ==
                               0) {
                        book_definition_offset = definition_offset;
                        book_definition_length = definition_length;
                } else {
                        CHECK(false, "class table entry names Person or Book");
                }
        }

        CHECK(person_id != 0, "Person class ID is present in the class table");
        CHECK(book_definition_offset != 0,
              "Book definition is present in the class table");
        if (person_id == 0 || book_definition_offset == 0) {
                free(bytes);
                remove(TEST_FILE);
                return;
        }
        if (book_definition_length < 16 ||
            !_range(file_size, book_definition_offset,
                    book_definition_length)) {
                CHECK(false, "Book definition range is valid");
                free(bytes);
                remove(TEST_FILE);
                return;
        }

        {
                const uint8_t* definition = bytes + book_definition_offset;
                size_t definition_end =
                    (size_t)book_definition_offset + book_definition_length;
                size_t descriptor_offset =
                    (size_t)book_definition_offset + 16 + _u16(definition);
                uint16_t field_name_length;

                CHECK(_u16(definition + 2) == 0 && _u32(definition + 4) == 0 &&
                          _u32(definition + 8) == 1 &&
                          _u32(definition + 12) == 0,
                      "Book definition stores flags, parent, field, and "
                      "interface counts");
                if (descriptor_offset > definition_end ||
                    definition_end - descriptor_offset < 11) {
                        CHECK(false,
                              "pointer field descriptor fits its definition");
                        free(bytes);
                        remove(TEST_FILE);
                        return;
                }

                field_name_length = _u16(bytes + descriptor_offset + 5);
                CHECK(_u32(bytes + descriptor_offset) != 0 &&
                          bytes[descriptor_offset + 4] == 5,
                      "pointer descriptor stores a field ID and pointer type "
                      "code");
                CHECK(field_name_length == strlen("author") &&
                          definition_end - descriptor_offset >=
                              11u + (size_t)field_name_length &&
                          _u32(bytes + descriptor_offset + 7) == person_id &&
                          memcmp(bytes + descriptor_offset + 11, "author",
                                 field_name_length) == 0,
                      "pointer descriptor stores target class ID before its "
                      "field name");
                CHECK(
                    descriptor_offset + 11 + field_name_length ==
                        definition_end,
                    "Book definition ends after its pointer field descriptor");
        }

        free(bytes);
        remove(TEST_FILE);
}

int main(void) {
        _test_pointer_field();
        printf("create_class_pointer tests:\n");
        printf("%d checks, %d failures\n", checks, failures);
        return failures != 0;
}
