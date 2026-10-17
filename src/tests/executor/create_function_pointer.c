/**
 * @file create_function_pointer.c
 * @brief Check pointer parameter descriptors against README.md.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interface/executor/executor.h"
#include "interface/storage/database.h"

#define TEST_FILE "build/create-function-pointer-test.oopdb"
#define CLASS_ENTRY_SIZE 48u
#define FUNCTION_ENTRY_SIZE 20u

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

static void _test_pointer_parameter(void) {
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
            {.name = "title", .type = {.kind = ANALYZER_TYPE_TEXT}},
        };
        struct analyzer_statement book = {
            .kind   = ANALYZER_CREATE_CLASS,
            .group  = ANALYZER_GROUP_SCHEMA,
            .name   = "Book",
            .fields = {.items = book_fields, .count = 1, .capacity = 1},
        };
        struct analyzer_field parameters[] = {
            {.name = "author",
             .type = {.kind = ANALYZER_TYPE_POINTER, .class_name = "Person"}},
        };
        struct analyzer_expression expression = {
            .kind  = ANALYZER_EXPRESSION_FIELD,
            .field = "author",
        };
        struct analyzer_statement function = {
            .kind        = ANALYZER_CREATE_FUNCTION,
            .group       = ANALYZER_GROUP_FUNCTION,
            .name        = "Book",
            .function    = "by_author",
            .fields      = {.items = parameters, .count = 1, .capacity = 1},
            .return_type = {.kind       = ANALYZER_TYPE_POINTER,
                            .class_name = "Person"},
            .expression  = &expression,
        };
        uint8_t* bytes;
        size_t file_size = 0;
        uint64_t class_table_offset;
        uint64_t class_table_length;
        uint64_t function_table_offset;
        uint64_t function_table_length;
        uint32_t person_id = 0;
        uint32_t book_id   = 0;
        const uint8_t* function_entry;
        uint64_t definition_offset;
        uint32_t definition_length;
        uint16_t function_name_length;
        uint32_t expression_length;
        size_t offset;
        size_t definition_end;
        size_t i;

        remove(TEST_FILE);
        CHECK(database_init_format(&db, TEST_FILE, true),
              "create README-format database");
        CHECK(i_execute(&db, &person) == EXECUTOR_OK,
              "CREATE CLASS Person succeeds");
        CHECK(i_execute(&db, &book) == EXECUTOR_OK,
              "CREATE CLASS Book succeeds");
        CHECK(i_execute(&db, &function) == EXECUTOR_OK,
              "CREATE FUNCTION Book.by_author succeeds");
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
                  _u16(bytes + 12) == 1 && _u16(bytes + 14) == 0,
              "header stores two classes, one function, and no interfaces");

        class_table_offset    = _u64(bytes + 32);
        class_table_length    = _u64(bytes + 40);
        function_table_offset = _u64(bytes + 48);
        function_table_length = _u64(bytes + 56);
        CHECK(_u64(bytes + 16) == 0 && _u64(bytes + 24) == 0 &&
                  class_table_length == 2 * CLASS_ENTRY_SIZE &&
                  function_table_length == FUNCTION_ENTRY_SIZE,
              "metatable points to two class entries and one function entry");
        if (class_table_length != 2 * CLASS_ENTRY_SIZE ||
            function_table_length != FUNCTION_ENTRY_SIZE ||
            class_table_offset < 64 || function_table_offset < 64 ||
            !_range(file_size, class_table_offset, class_table_length) ||
            !_range(file_size, function_table_offset, function_table_length)) {
                free(bytes);
                remove(TEST_FILE);
                return;
        }

        for (i = 0; i < 2; i++) {
                const uint8_t* entry =
                    bytes + class_table_offset + i * CLASS_ENTRY_SIZE;
                uint32_t class_id         = _u32(entry);
                uint64_t class_def        = _u64(entry + 4);
                uint32_t class_def_length = _u32(entry + 12);
                uint16_t class_name_length;
                const uint8_t* definition;

                if (class_def_length < 16 ||
                    !_range(file_size, class_def, class_def_length)) {
                        CHECK(false, "class definition range is valid");
                        continue;
                }
                definition        = bytes + class_def;
                class_name_length = _u16(definition);
                if (class_name_length > class_def_length - 16) {
                        CHECK(false, "class name fits its definition");
                        continue;
                }
                if (class_name_length == strlen("Person") &&
                    memcmp(definition + 16, "Person", class_name_length) == 0) {
                        person_id = class_id;
                } else if (class_name_length == strlen("Book") &&
                           memcmp(definition + 16, "Book", class_name_length) ==
                               0) {
                        book_id = class_id;
                } else {
                        CHECK(false, "class table entry names Person or Book");
                }
        }
        CHECK(person_id != 0 && book_id != 0 && person_id != book_id,
              "class table provides distinct Person and Book IDs");
        if (person_id == 0 || book_id == 0) {
                free(bytes);
                remove(TEST_FILE);
                return;
        }

        function_entry = bytes + function_table_offset;
        CHECK(_u32(function_entry) != 0 && _u32(function_entry + 4) == book_id,
              "function table entry stores its ID and Book owner ID");
        definition_offset = _u64(function_entry + 8);
        definition_length = _u32(function_entry + 16);
        CHECK(definition_length >= 9 &&
                  definition_offset >=
                      function_table_offset + function_table_length &&
                  _range(file_size, definition_offset, definition_length),
              "function definition follows the table and fits in the file");
        if (definition_length < 9 ||
            definition_offset < function_table_offset + function_table_length ||
            !_range(file_size, definition_offset, definition_length)) {
                free(bytes);
                remove(TEST_FILE);
                return;
        }

        offset               = (size_t)definition_offset;
        definition_end       = offset + definition_length;
        function_name_length = _u16(bytes + offset);
        CHECK(_u16(bytes + offset + 2) == 1 && bytes[offset + 4] == 5 &&
                  _u32(bytes + offset + 5) == person_id,
              "function definition stores one parameter and pointer return "
              "type");
        expression_length = _u32(bytes + offset + 9);
        offset += 13;
        CHECK(
            function_name_length == strlen("by_author") &&
                function_name_length <= definition_end - offset &&
                memcmp(bytes + offset, "by_author", function_name_length) == 0,
            "function definition stores its length-prefixed name");
        if (function_name_length > definition_end - offset) {
                free(bytes);
                remove(TEST_FILE);
                return;
        }
        offset += function_name_length;

        if (offset > definition_end || definition_end - offset < 7) {
                free(bytes);
                remove(TEST_FILE);
                return;
        }
        {
                uint16_t parameter_name_length = _u16(bytes + offset);

                CHECK(parameter_name_length == strlen("author") &&
                          bytes[offset + 2] == 5 &&
                          definition_end - offset >=
                              7u + (size_t)parameter_name_length &&
                          _u32(bytes + offset + 3) == person_id &&
                          memcmp(bytes + offset + 7, "author",
                                 parameter_name_length) == 0,
                      "pointer parameter stores type code, target class ID, "
                      "then name");
                if (parameter_name_length > definition_end - offset - 7) {
                        free(bytes);
                        remove(TEST_FILE);
                        return;
                }
                offset += 7 + parameter_name_length;
        }
        CHECK(expression_length == strlen("author") &&
                  expression_length <= definition_end - offset &&
                  memcmp(bytes + offset, "author", expression_length) == 0,
              "function definition stores its length-prefixed expression");
        offset += expression_length;
        CHECK(offset == definition_end,
              "function definition ends after its expression");

        free(bytes);
        remove(TEST_FILE);
}

int main(void) {
        _test_pointer_parameter();
        printf("create_function_pointer tests:\n");
        printf("%d checks, %d failures\n", checks, failures);
        return failures != 0;
}
