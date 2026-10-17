/**
 * @file create_function.c
 * @brief Check CREATE FUNCTION output against the binary layout in README.md.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interface/executor/executor.h"
#include "interface/storage/database.h"

#define TEST_FILE "build/create-function-test.oopdb"
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

static uint32_t _find_function_id(const uint8_t* bytes, size_t file_size,
                                  const char* expected_name) {
        uint64_t table_offset;
        uint16_t count;
        size_t index;

        if (file_size < 64) {
                return 0;
        }
        count        = _u16(bytes + 12);
        table_offset = _u64(bytes + 48);
        if (!_range(file_size, table_offset,
                    (uint64_t)count * FUNCTION_ENTRY_SIZE)) {
                return 0;
        }
        for (index = 0; index < count; index++) {
                const uint8_t* entry =
                    bytes + table_offset + index * FUNCTION_ENTRY_SIZE;
                uint64_t definition_offset = _u64(entry + 8);
                uint32_t definition_length = _u32(entry + 16);
                uint16_t name_length;

                if (definition_length < 9 ||
                    !_range(file_size, definition_offset, definition_length)) {
                        continue;
                }
                name_length = _u16(bytes + definition_offset);
                if (name_length <= definition_length - 9 &&
                    name_length == strlen(expected_name) &&
                    memcmp(bytes + definition_offset + 9, expected_name,
                           name_length) == 0) {
                        return _u32(entry);
                }
        }
        return 0;
}

static void _test_create_function_bytes(void) {
        struct database db;
        struct analyzer_field class_fields[] = {
            {.name = "salary", .type = {.kind = ANALYZER_TYPE_FLOAT}},
        };
        struct analyzer_statement class_statement = {
            .kind   = ANALYZER_CREATE_CLASS,
            .group  = ANALYZER_GROUP_SCHEMA,
            .name   = "Employee",
            .fields = {.items = class_fields, .count = 1, .capacity = 1},
        };
        struct analyzer_field parameters[] = {
            {.name = "percent", .type = {.kind = ANALYZER_TYPE_FLOAT}},
        };
        struct analyzer_expression expression = {
            .kind  = ANALYZER_EXPRESSION_FIELD,
            .field = "percent",
        };
        struct analyzer_statement function_statement = {
            .kind        = ANALYZER_CREATE_FUNCTION,
            .group       = ANALYZER_GROUP_FUNCTION,
            .name        = "Employee",
            .function    = "raise",
            .fields      = {.items = parameters, .count = 1, .capacity = 1},
            .return_type = {.kind = ANALYZER_TYPE_FLOAT},
            .expression  = &expression,
        };
        uint8_t* bytes;
        size_t file_size = 0;
        uint64_t class_table_offset;
        uint64_t class_table_length;
        uint64_t function_table_offset;
        uint64_t function_table_length;
        uint32_t class_id;
        const uint8_t* function_entry;
        uint32_t function_id;
        uint32_t owner_class_id;
        uint64_t definition_offset;
        uint32_t definition_length;
        struct analyzer_statement drop_statement = {
            .kind     = ANALYZER_DROP_FUNCTION,
            .group    = ANALYZER_GROUP_FUNCTION,
            .name     = "Employee",
            .function = "raise",
        };
        struct analyzer_statement second_function_statement;
        size_t offset;
        size_t definition_end;
        uint16_t name_length;
        uint16_t parameter_name_length;
        uint32_t expression_length;
        uint32_t retained_function_id;

        remove(TEST_FILE);
        CHECK(database_init_format(&db, TEST_FILE, true),
              "create README-format database");
        CHECK(i_execute(&db, &class_statement) == EXECUTOR_OK,
              "CREATE CLASS Employee succeeds");
        CHECK(i_execute(&db, &function_statement) == EXECUTOR_OK,
              "CREATE FUNCTION Employee.raise succeeds");
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
        CHECK(_u16(bytes + 8) == 0 && _u16(bytes + 10) == 1 &&
                  _u16(bytes + 12) == 1 && _u16(bytes + 14) == 0,
              "header stores one class, one function, and no interfaces");

        class_table_offset    = _u64(bytes + 32);
        class_table_length    = _u64(bytes + 40);
        function_table_offset = _u64(bytes + 48);
        function_table_length = _u64(bytes + 56);
        CHECK(_u64(bytes + 16) == 0 && _u64(bytes + 24) == 0 &&
                  class_table_length == CLASS_ENTRY_SIZE &&
                  function_table_length == FUNCTION_ENTRY_SIZE,
              "metatable points to one class and one function table entry");
        if (class_table_length != CLASS_ENTRY_SIZE ||
            function_table_length != FUNCTION_ENTRY_SIZE ||
            !_range(file_size, class_table_offset, class_table_length) ||
            !_range(file_size, function_table_offset, function_table_length)) {
                free(bytes);
                remove(TEST_FILE);
                return;
        }

        class_id = _u32(bytes + class_table_offset);
        CHECK(class_id != 0, "class table entry stores a stable class ID");
        function_entry    = bytes + function_table_offset;
        function_id       = _u32(function_entry);
        owner_class_id    = _u32(function_entry + 4);
        definition_offset = _u64(function_entry + 8);
        definition_length = _u32(function_entry + 16);
        CHECK(function_id != 0 && owner_class_id == class_id,
              "function table entry stores its ID and owner class ID");
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

        offset         = (size_t)definition_offset;
        definition_end = offset + definition_length;
        name_length    = _u16(bytes + offset);
        CHECK(_u16(bytes + offset + 2) == 1 && bytes[offset + 4] == 2,
              "function definition stores one parameter and float return type");
        expression_length = _u32(bytes + offset + 5);
        offset += 9;
        CHECK(name_length == strlen("raise") &&
                  name_length <= definition_end - offset &&
                  memcmp(bytes + offset, "raise", name_length) == 0,
              "function definition stores its length-prefixed name");
        if (name_length > definition_end - offset) {
                free(bytes);
                remove(TEST_FILE);
                return;
        }
        offset += name_length;

        if (definition_end - offset < 3) {
                free(bytes);
                remove(TEST_FILE);
                return;
        }
        parameter_name_length = _u16(bytes + offset);
        CHECK(parameter_name_length == strlen("percent") &&
                  bytes[offset + 2] == 2 &&
                  parameter_name_length <= definition_end - offset - 3 &&
                  memcmp(bytes + offset + 3, "percent",
                         parameter_name_length) == 0,
              "parameter descriptor stores a float type and its name");
        if (parameter_name_length > definition_end - offset - 3) {
                free(bytes);
                remove(TEST_FILE);
                return;
        }
        offset += 3 + parameter_name_length;
        CHECK(expression_length == strlen("percent") &&
                  expression_length <= definition_end - offset &&
                  memcmp(bytes + offset, "percent", expression_length) == 0,
              "function definition stores its length-prefixed expression");
        offset += expression_length;
        CHECK(offset == definition_end,
              "function definition ends after its expression");

        free(bytes);

        CHECK(database_init_format(&db, TEST_FILE, false),
              "reopen database before creating a retained function");
        second_function_statement          = function_statement;
        second_function_statement.function = "bonus";
        CHECK(i_execute(&db, &second_function_statement) == EXECUTOR_OK,
              "create a second function before dropping the first");
        database_close(&db);

        bytes = _read_file(&file_size);
        CHECK(bytes != NULL, "read function table before DROP FUNCTION");
        retained_function_id = 0;
        if (bytes != NULL) {
                uint64_t table_offset = _u64(bytes + 48);
                uint64_t table_length = _u64(bytes + 56);

                CHECK(_u16(bytes + 12) == 2 &&
                          table_length == 2 * FUNCTION_ENTRY_SIZE,
                      "two functions occupy two table entries");
                CHECK(_range(file_size, table_offset, table_length),
                      "two-entry function table fits in the file");
                retained_function_id =
                    _find_function_id(bytes, file_size, "bonus");
                CHECK(retained_function_id != 0 &&
                          retained_function_id != function_id,
                      "the retained function has a distinct stable ID");
                free(bytes);
        }

        CHECK(database_init_format(&db, TEST_FILE, false),
              "open database for DROP FUNCTION");
        CHECK(i_execute(&db, &drop_statement) == EXECUTOR_OK,
              "DROP FUNCTION succeeds");
        database_close(&db);

        bytes = _read_file(&file_size);
        CHECK(bytes != NULL, "read database bytes after DROP FUNCTION");
        if (bytes != NULL) {
                uint64_t table_offset = _u64(bytes + 48);
                uint64_t table_length = _u64(bytes + 56);

                CHECK(_u16(bytes + 12) == 1 &&
                          table_length == FUNCTION_ENTRY_SIZE,
                      "header and metatable retain one function table entry");
                CHECK(_range(file_size, table_offset, table_length),
                      "retained function table range fits in the file");
                CHECK(_find_function_id(bytes, file_size, "bonus") ==
                          retained_function_id,
                      "DROP FUNCTION preserves the retained function ID");
                free(bytes);
        }
        remove(TEST_FILE);
}

int main(void) {
        _test_create_function_bytes();
        printf("create_function tests:\n");
        printf("%d checks, %d failures\n", checks, failures);
        return failures != 0;
}
