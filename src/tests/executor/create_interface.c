/**
 * @file create_interface.c
 * @brief Independent unit tests for CREATE INTERFACE execution and physical
 * storage layout.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interface/executor/executor.h"
#include "interface/storage/database.h"
#include "interface/storage/header.h"
#include "interface/storage/page.h"

#define TEST_FILE "build/create-interface-test.oopdb"

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

static uint16_t _read_u16(const uint8_t* p) {
        return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t _read_u32(const uint8_t* p) {
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
               ((uint32_t)p[3] << 24);
}

/**
 * Manually read and verify raw file bytes on disk without using database helper
 * functions.
 */
static void _verify_raw_disk_layout(const char* path, const char* expected_name,
                                    uint32_t* out_loc_page) {
        FILE* fp = fopen(path, "rb");
        uint8_t header_buf[STORAGE_HEADER_SIZE];
        uint8_t dir_page_buf[DB_PAGE_SIZE];
        uint8_t def_page_buf[DB_PAGE_SIZE];
        uint32_t iface_count;
        uint32_t iface_dir_page;
        uint16_t name_len;
        uint32_t loc_page;
        uint16_t loc_off;
        uint16_t loc_len;
        size_t expected_name_len;

        CHECK(fp != NULL, "raw file open succeeds");
        if (fp == NULL) return;

        /* 1. Verify header bytes at offset 0 */
        CHECK(fread(header_buf, 1, STORAGE_HEADER_SIZE, fp) ==
                  STORAGE_HEADER_SIZE,
              "raw header read succeeds");
        CHECK(memcmp(header_buf + STORAGE_HDR_MAGIC_OFF, STORAGE_MAGIC,
                     STORAGE_MAGIC_SIZE) == 0,
              "raw header magic matches");
        CHECK(
            _read_u32(header_buf + STORAGE_HDR_VERSION_OFF) == STORAGE_VERSION,
            "raw header version matches");
        CHECK(_read_u32(header_buf + STORAGE_HDR_PAGE_SIZE_OFF) == DB_PAGE_SIZE,
              "raw header page size matches");

        iface_count = _read_u32(header_buf + STORAGE_HDR_IFACE_COUNT_OFF);
        CHECK(iface_count == 1, "raw header iface_count is 1");

        iface_dir_page = _read_u32(header_buf + STORAGE_HDR_IFACE_DIR_PAGE_OFF);
        CHECK(iface_dir_page > 0, "raw header iface_dir_page is allocated");

        /* 2. Verify interface directory page at (iface_dir_page * DB_PAGE_SIZE)
         */
        CHECK(fseek(fp, (long)iface_dir_page * DB_PAGE_SIZE, SEEK_SET) == 0,
              "seek to iface dir page succeeds");
        CHECK(fread(dir_page_buf, 1, DB_PAGE_SIZE, fp) == DB_PAGE_SIZE,
              "read iface dir page succeeds");

        /* Directory page layout (from page.h):
         * offset 0..1: count (u16)
         * offset 2..3: free_off (u16)
         * offset 4..7: next_page (u32)
         * offset 8 (DIR_PAGE_HDR_SIZE): first entry starts here
         * entry format:
         *   +0..1: name_len (u16)
         *   +2..5: loc_page (u32)
         *   +6..7: loc_off (u16)
         *   +8..9: loc_len (u16)
         *   +10..: name (UTF-8, name_len bytes)
         */
        uint16_t entry_offset = DIR_PAGE_HDR_SIZE;
        name_len              = _read_u16(dir_page_buf + entry_offset);
        loc_page              = _read_u32(dir_page_buf + entry_offset + 2);
        loc_off               = _read_u16(dir_page_buf + entry_offset + 6);
        loc_len               = _read_u16(dir_page_buf + entry_offset + 8);

        expected_name_len = strlen(expected_name);
        CHECK(name_len == expected_name_len,
              "raw dir entry name length matches");
        CHECK(memcmp(dir_page_buf + entry_offset + 10, expected_name,
                     expected_name_len) == 0,
              "raw dir entry name string matches");
        CHECK(loc_off == 0, "raw dir entry loc_off is 0");
        CHECK(loc_len == 28, "raw dir entry loc_len is 28");

        if (out_loc_page) {
                *out_loc_page = loc_page;
        }

        /* 3. Verify interface definition page at (loc_page * DB_PAGE_SIZE) */
        CHECK(fseek(fp, (long)loc_page * DB_PAGE_SIZE, SEEK_SET) == 0,
              "seek to definition page succeeds");
        CHECK(fread(def_page_buf, 1, DB_PAGE_SIZE, fp) == DB_PAGE_SIZE,
              "read definition page succeeds");

        /* Interface definition format:
         *   u16 field_count
         *   repeated:
         *     u16 type_length
         *     type bytes
         *     u16 name_length
         *     name bytes
         */
        CHECK(_read_u16(def_page_buf) == 2, "raw definition field_count is 2");
        /* Field 0: type "text", name "name" */
        CHECK(_read_u16(def_page_buf + 2) == 4, "raw field 0 type length is 4");
        CHECK(memcmp(def_page_buf + 4, "text", 4) == 0,
              "raw field 0 type is text");
        CHECK(_read_u16(def_page_buf + 8) == 4, "raw field 0 name length is 4");
        CHECK(memcmp(def_page_buf + 10, "name", 4) == 0,
              "raw field 0 name is name");

        /* Field 1: type "bool", name "active" */
        CHECK(_read_u16(def_page_buf + 14) == 4,
              "raw field 1 type length is 4");
        CHECK(memcmp(def_page_buf + 16, "bool", 4) == 0,
              "raw field 1 type is bool");
        CHECK(_read_u16(def_page_buf + 20) == 6,
              "raw field 1 name length is 6");
        CHECK(memcmp(def_page_buf + 22, "active", 6) == 0,
              "raw field 1 name is active");

        fclose(fp);
}

static void test_create_interface_success_and_manual_check(void) {
        struct database db;
        uint32_t loc_page              = 0;
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

        remove(TEST_FILE);
        database_init(&db, TEST_FILE, true);

        CHECK(i_execute(&db, &statement) == EXECUTOR_OK,
              "execute CREATE INTERFACE succeeds");
        CHECK(db.hdr.iface_count == 1, "db header reflects iface_count == 1");

        /* Save & close database so all pages and header are flushed to disk */
        database_close(&db);

        /* Manually inspect raw bytes on disk without helper functions */
        _verify_raw_disk_layout(TEST_FILE, "UserInterface", &loc_page);

        /* Reopen database and verify high-level storage retrieval matches
         * manual check */
        database_init(&db, TEST_FILE, false);
        {
                uint32_t found_page;
                uint16_t found_off;
                uint16_t found_len;
                uint8_t page_buf[DB_PAGE_SIZE];

                CHECK(db_interface_dir_find(&db, "UserInterface", &found_page,
                                            &found_off, &found_len),
                      "db_interface_dir_find locates interface after reopen");
                CHECK(found_page == loc_page,
                      "directory found_page matches manual loc_page");
                CHECK(found_off == 0 && found_len == 28,
                      "directory offset and length match");
                CHECK(db_read_page(&db, found_page, page_buf),
                      "db_read_page succeeds on definition page");
                CHECK(_read_u16(page_buf) == 2, "definition field count is 2");
        }
        database_close(&db);
        remove(TEST_FILE);
}

static void test_create_interface_negative_cases(void) {
        struct database db;
        remove(TEST_FILE);
        database_init(&db, TEST_FILE, true);

        /* 1. Missing statement or db */
        CHECK(i_execute(NULL, NULL) == EXECUTOR_ERROR_BAD_STATEMENT,
              "rejects null db and statement");
        CHECK(i_execute(&db, NULL) == EXECUTOR_ERROR_BAD_STATEMENT,
              "rejects null statement");

        /* 2. Empty or missing interface name */
        struct analyzer_statement no_name = {
            .kind  = ANALYZER_CREATE_INTERFACE,
            .group = ANALYZER_GROUP_SCHEMA,
            .name  = NULL,
        };
        CHECK(i_execute(&db, &no_name) == EXECUTOR_ERROR_BAD_STATEMENT,
              "rejects null interface name");

        struct analyzer_statement empty_name = {
            .kind  = ANALYZER_CREATE_INTERFACE,
            .group = ANALYZER_GROUP_SCHEMA,
            .name  = "",
        };
        CHECK(i_execute(&db, &empty_name) == EXECUTOR_ERROR_BAD_STATEMENT,
              "rejects empty interface name");

        /* 3. Empty field name */
        struct analyzer_field empty_field[] = {
            {.name = "", .type = {.kind = ANALYZER_TYPE_INTEGER}},
        };
        struct analyzer_statement empty_field_stmt = {
            .kind   = ANALYZER_CREATE_INTERFACE,
            .group  = ANALYZER_GROUP_SCHEMA,
            .name   = "EmptyFieldIface",
            .fields = {.items = empty_field, .count = 1, .capacity = 1},
        };
        CHECK(i_execute(&db, &empty_field_stmt) == EXECUTOR_ERROR_BAD_STATEMENT,
              "rejects empty field name");

        /* 4. Duplicate field names */
        struct analyzer_field dup_fields[] = {
            {.name = "age", .type = {.kind = ANALYZER_TYPE_INTEGER}},
            {.name = "age", .type = {.kind = ANALYZER_TYPE_FLOAT}},
        };
        struct analyzer_statement dup_field_stmt = {
            .kind   = ANALYZER_CREATE_INTERFACE,
            .group  = ANALYZER_GROUP_SCHEMA,
            .name   = "DupFieldIface",
            .fields = {.items = dup_fields, .count = 2, .capacity = 2},
        };
        CHECK(i_execute(&db, &dup_field_stmt) == EXECUTOR_ERROR_BAD_STATEMENT,
              "rejects duplicate field name");

        /* 5. Pointer type without target class name */
        struct analyzer_field invalid_ptr_field[] = {
            {.name = "target",
             .type = {.kind = ANALYZER_TYPE_POINTER, .class_name = NULL}},
        };
        struct analyzer_statement invalid_ptr_stmt = {
            .kind   = ANALYZER_CREATE_INTERFACE,
            .group  = ANALYZER_GROUP_SCHEMA,
            .name   = "InvalidPtrIface",
            .fields = {.items = invalid_ptr_field, .count = 1, .capacity = 1},
        };
        CHECK(i_execute(&db, &invalid_ptr_stmt) == EXECUTOR_ERROR_BAD_STATEMENT,
              "rejects pointer field without target class name");

        /* 6. Create valid interface, then reject duplicate interface name */
        struct analyzer_field valid_fields[] = {
            {.name = "x", .type = {.kind = ANALYZER_TYPE_FLOAT}},
        };
        struct analyzer_statement valid_stmt = {
            .kind   = ANALYZER_CREATE_INTERFACE,
            .group  = ANALYZER_GROUP_SCHEMA,
            .name   = "PointIface",
            .fields = {.items = valid_fields, .count = 1, .capacity = 1},
        };
        CHECK(i_execute(&db, &valid_stmt) == EXECUTOR_OK,
              "initial create interface succeeds");
        CHECK(i_execute(&db, &valid_stmt) == EXECUTOR_ERROR_BAD_STATEMENT,
              "duplicate interface creation is rejected");

        database_close(&db);
        remove(TEST_FILE);
}

int main(void) {
        test_create_interface_success_and_manual_check();
        test_create_interface_negative_cases();

        printf("create_interface tests:\n");
        printf("%d checks, %d failures\n", checks, failures);
        return failures != 0;
}
