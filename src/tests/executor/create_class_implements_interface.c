/**
 * @file create_class_implements_interface.c
 * @brief Check interface and class definitions against README.md.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interface/executor/executor.h"
#include "interface/storage/database.h"

#define TEST_FILE "build/create-class-implements-interface-test.oopdb"

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

static void _test_create_class_implements_interface(void) {
        struct database db;
        struct analyzer_field fields[] = {
            {.name = "name", .type = {.kind = ANALYZER_TYPE_TEXT}},
        };
        struct analyzer_statement interface_statement = {
            .kind   = ANALYZER_CREATE_INTERFACE,
            .group  = ANALYZER_GROUP_SCHEMA,
            .name   = "Named",
            .fields = {.items = fields, .count = 1, .capacity = 1},
        };
        char* interfaces[]                        = {"Named"};
        struct analyzer_statement class_statement = {
            .kind       = ANALYZER_CREATE_CLASS,
            .group      = ANALYZER_GROUP_SCHEMA,
            .name       = "Person",
            .implements = {.items = interfaces, .count = 1, .capacity = 1},
            .fields     = {.items = fields, .count = 1, .capacity = 1},
        };
        uint8_t* bytes;
        size_t file_size = 0;
        uint64_t interface_table_offset;
        uint64_t interface_table_length;
        uint64_t class_table_offset;
        uint64_t class_table_length;
        uint64_t interface_definition_offset;
        uint32_t interface_definition_length;
        uint64_t class_definition_offset;
        uint32_t class_definition_length;
        uint32_t interface_id;
        uint32_t class_field_id;
        uint32_t interface_field_id;
        uint16_t name_length;
        uint16_t field_name_length;
        size_t offset;
        size_t definition_end;
        const uint8_t* interface_entry;
        const uint8_t* class_entry;

        remove(TEST_FILE);
        CHECK(database_init_format(&db, TEST_FILE, true),
              "create README-format database");
        CHECK(i_execute(&db, &interface_statement) == EXECUTOR_OK,
              "CREATE INTERFACE succeeds");
        CHECK(i_execute(&db, &class_statement) == EXECUTOR_OK,
              "CREATE CLASS implementing it succeeds");
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
        CHECK(_u16(bytes + 8) == 1 && _u16(bytes + 10) == 1 &&
                  _u16(bytes + 12) == 0 && _u16(bytes + 14) == 0,
              "header stores one interface, one class, and no functions");

        interface_table_offset = _u64(bytes + 16);
        interface_table_length = _u64(bytes + 24);
        class_table_offset     = _u64(bytes + 32);
        class_table_length     = _u64(bytes + 40);
        CHECK(interface_table_length == 16 && class_table_length == 48 &&
                  _u64(bytes + 48) == 0 && _u64(bytes + 56) == 0,
              "metatable stores the interface and class table ranges");
        if (interface_table_length != 16 || class_table_length != 48 ||
            interface_table_offset < 64 || class_table_offset < 64 ||
            !_range(file_size, interface_table_offset,
                    interface_table_length) ||
            !_range(file_size, class_table_offset, class_table_length)) {
                free(bytes);
                remove(TEST_FILE);
                return;
        }

        interface_entry             = bytes + interface_table_offset;
        class_entry                 = bytes + class_table_offset;
        interface_id                = _u32(interface_entry);
        interface_definition_offset = _u64(interface_entry + 4);
        interface_definition_length = _u32(interface_entry + 12);
        CHECK(interface_id != 0,
              "interface table entry stores a stable interface ID");
        CHECK(interface_definition_length >= 4 &&
                  interface_definition_offset >=
                      interface_table_offset + interface_table_length &&
                  _range(file_size, interface_definition_offset,
                         interface_definition_length),
              "interface definition range is valid");
        if (interface_definition_length < 4 ||
            interface_definition_offset <
                interface_table_offset + interface_table_length ||
            !_range(file_size, interface_definition_offset,
                    interface_definition_length)) {
                free(bytes);
                remove(TEST_FILE);
                return;
        }

        offset         = (size_t)interface_definition_offset;
        definition_end = offset + interface_definition_length;
        name_length    = _u16(bytes + offset);
        CHECK(_u16(bytes + offset + 2) == 1,
              "interface definition stores one required field");
        offset += 4;
        CHECK(name_length == strlen("Named") &&
                  name_length <= definition_end - offset &&
                  memcmp(bytes + offset, "Named", name_length) == 0,
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
        interface_field_id = _u32(bytes + offset);
        CHECK(interface_field_id != 0 && bytes[offset + 4] == 3,
              "interface field has an ID and text type code");
        field_name_length = _u16(bytes + offset + 5);
        offset += 7;
        CHECK(field_name_length == strlen("name") &&
                  field_name_length <= definition_end - offset &&
                  memcmp(bytes + offset, "name", field_name_length) == 0,
              "interface field descriptor stores its name");
        if (field_name_length > definition_end - offset) {
                free(bytes);
                remove(TEST_FILE);
                return;
        }
        offset += field_name_length;
        CHECK(offset == definition_end,
              "interface definition ends after its field descriptor");

        class_definition_offset = _u64(class_entry + 4);
        class_definition_length = _u32(class_entry + 12);
        CHECK(_u32(class_entry) != 0,
              "class table entry stores a stable class ID");
        CHECK(class_definition_length >= 16 &&
                  class_definition_offset >=
                      class_table_offset + class_table_length &&
                  _range(file_size, class_definition_offset,
                         class_definition_length),
              "class definition range is valid");
        if (class_definition_length < 16 ||
            class_definition_offset < class_table_offset + class_table_length ||
            !_range(file_size, class_definition_offset,
                    class_definition_length)) {
                free(bytes);
                remove(TEST_FILE);
                return;
        }

        offset         = (size_t)class_definition_offset;
        definition_end = offset + class_definition_length;
        name_length    = _u16(bytes + offset);
        CHECK(_u16(bytes + offset + 2) == 0 && _u32(bytes + offset + 4) == 0 &&
                  _u32(bytes + offset + 8) == 1 &&
                  _u32(bytes + offset + 12) == 1,
              "class definition stores flags, no parent, one field, and one "
              "interface");
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
        class_field_id = _u32(bytes + offset);
        CHECK(class_field_id != 0 && class_field_id != interface_field_id &&
                  bytes[offset + 4] == 3,
              "class field has a unique ID and matching text type code");
        field_name_length = _u16(bytes + offset + 5);
        offset += 7;
        CHECK(field_name_length == strlen("name") &&
                  field_name_length <= definition_end - offset &&
                  memcmp(bytes + offset, "name", field_name_length) == 0,
              "class field descriptor stores its name");
        if (field_name_length > definition_end - offset) {
                free(bytes);
                remove(TEST_FILE);
                return;
        }
        offset += field_name_length;
        CHECK(definition_end - offset >= 4 &&
                  _u32(bytes + offset) == interface_id,
              "class definition stores the implemented interface ID");
        offset += 4;
        CHECK(offset == definition_end,
              "class definition ends after its interface IDs");

        free(bytes);
        remove(TEST_FILE);
}

int main(void) {
        _test_create_class_implements_interface();
        printf("create_class_implements_interface tests:\n");
        printf("%d checks, %d failures\n", checks, failures);
        return failures != 0;
}
