/**
 * @file main.c
 * @brief File check unit tests, run with `make test` (needs build/).
 */

#include "interface/file-check/main.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "implementation/executor/helper/create_header.h"
#include "implementation/executor/helper/create_metatable.h"
#include "interface/storage/header.h"

#define TEST_FILE "build/file-check-test.oopdb"
#define FORMAT_FILE_SIZE \
        (EXECUTOR_FORMAT_HEADER_SIZE + EXECUTOR_FORMAT_METATABLE_SIZE)

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

/** Write bytes to the test file. */
static void _write_file(const unsigned char* bytes, size_t count) {
        FILE* file = fopen(TEST_FILE, "wb");

        if (file == NULL) {
                printf("cannot create %s, run from the project root\n",
                       TEST_FILE);
                exit(2);
        }
        fwrite(bytes, 1, count, file);
        fclose(file);
}

/** Write a README-format file with the given magic and class count. */
static void _write_header(const char* magic, unsigned long class_count) {
        unsigned char bytes[FORMAT_FILE_SIZE];
        struct executor_format_header header       = {0};
        struct executor_format_metatable metatable = {0};

        header.class_count = (uint16_t)class_count;
        (void)executor_helper_create_header(bytes, sizeof bytes, &header);
        (void)executor_helper_create_metatable(
            bytes + EXECUTOR_FORMAT_HEADER_SIZE,
            sizeof bytes - EXECUTOR_FORMAT_HEADER_SIZE, &metatable);
        memcpy(bytes, magic, STORAGE_MAGIC_SIZE);
        _write_file(bytes, sizeof bytes);
}

/** Run the check for a statement kind and compare state and error. */
static void _expect(const char* path, enum ANALYZER_STATEMENT kind,
                    enum FILE_CHECK_STATE state,
                    enum file_check_error_type type, const char* name) {
        struct analyzer_statement statement = {ANALYZER_INVALID};
        struct file_check_error error;
        enum FILE_CHECK_STATE got;

        statement.kind = kind;
        got            = i_file_check(path, &statement, &error);
        CHECK(got == state, name);
        CHECK(error.type == type, name);
        CHECK(error.path == path, name);
}

/** True if the test file exists. */
static int _exists(void) {
        FILE* file = fopen(TEST_FILE, "rb");

        if (file != NULL) {
                fclose(file);
        }
        return file != NULL;
}

static void _test_missing(void) {
        enum ANALYZER_STATEMENT writing[] = {ANALYZER_CREATE_CLASS,
                                             ANALYZER_CREATE_INTERFACE,
                                             ANALYZER_NEW,
                                             ANALYZER_SET,
                                             ANALYZER_DELETE,
                                             ANALYZER_DROP_CLASS,
                                             ANALYZER_ALTER_CLASS_ADD_FIELD};

        enum ANALYZER_STATEMENT reading[] = {
            ANALYZER_FIND, ANALYZER_LIST_CLASSES, ANALYZER_LIST_INTERFACES,
            ANALYZER_DESCRIBE, ANALYZER_CALL};

        size_t i;

        for (i = 0; i < sizeof reading / sizeof reading[0]; i++) {
                remove(TEST_FILE);
                _expect(TEST_FILE, reading[i], FILE_CHECK_STATE_FAILED,
                        FILE_CHECK_ERROR_NOT_FOUND,
                        "missing: reading statement needs existing data");
                CHECK(!_exists(), "missing: reading statement creates nothing");
        }
        for (i = 0; i < sizeof writing / sizeof writing[0]; i++) {
                unsigned char bytes[FORMAT_FILE_SIZE] = {0};
                FILE* file;
                size_t got = 0;

                remove(TEST_FILE);
                _expect(TEST_FILE, writing[i], FILE_CHECK_STATE_CREATED,
                        FILE_CHECK_OK, "missing: writing statement creates");
                file = fopen(TEST_FILE, "rb");
                if (file != NULL) {
                        got = fread(bytes, 1, sizeof bytes, file);
                        CHECK(fgetc(file) == EOF, "created: fixed header only");
                        fclose(file);
                }
                CHECK(
                    got == FORMAT_FILE_SIZE &&
                        memcmp(bytes, STORAGE_MAGIC, STORAGE_MAGIC_SIZE) == 0 &&
                        bytes[8] == 0 && bytes[9] == 0 && bytes[10] == 0 &&
                        bytes[11] == 0 && bytes[14] == 0 && bytes[15] == 0,
                    "created: README magic, counts, and reserved field");
                _expect(TEST_FILE, writing[i], FILE_CHECK_STATE_READY,
                        FILE_CHECK_OK, "created: second check is ready");
        }
}

static void _test_valid(void) {
        _write_header(STORAGE_MAGIC, 0);
        _expect(TEST_FILE, ANALYZER_FIND, FILE_CHECK_STATE_READY, FILE_CHECK_OK,
                "valid: read");
        _expect(TEST_FILE, ANALYZER_DESCRIBE, FILE_CHECK_STATE_READY,
                FILE_CHECK_OK, "valid: describe");
        _expect(TEST_FILE, ANALYZER_SET, FILE_CHECK_STATE_READY, FILE_CHECK_OK,
                "valid: write");
        _expect(TEST_FILE, ANALYZER_CREATE_CLASS, FILE_CHECK_STATE_READY,
                FILE_CHECK_OK, "valid: create on an existing database");
}

static void _test_broken(void) {
        unsigned char text[]                = "not a database at all";
        struct analyzer_statement statement = {ANALYZER_INVALID};
        struct file_check_error error;

        statement.kind = ANALYZER_FIND;
        _write_file(text, 0);
        _expect(TEST_FILE, ANALYZER_FIND, FILE_CHECK_STATE_FAILED,
                FILE_CHECK_ERROR_TOO_SHORT, "broken: empty file");
        _write_file(text, 5);
        _expect(TEST_FILE, ANALYZER_FIND, FILE_CHECK_STATE_FAILED,
                FILE_CHECK_ERROR_TOO_SHORT, "broken: short file");
        _write_header("notdb\0\0\0", STORAGE_VERSION);
        _expect(TEST_FILE, ANALYZER_FIND, FILE_CHECK_STATE_FAILED,
                FILE_CHECK_ERROR_BAD_MAGIC, "broken: wrong magic");
        _write_header("OOPDX\0\0\0", STORAGE_VERSION);
        _expect(TEST_FILE, ANALYZER_NEW, FILE_CHECK_STATE_FAILED,
                FILE_CHECK_ERROR_BAD_MAGIC, "broken: almost the magic");
        _write_header(STORAGE_MAGIC, 1);
        _expect(TEST_FILE, ANALYZER_FIND, FILE_CHECK_STATE_FAILED,
                FILE_CHECK_ERROR_BAD_LAYOUT, "broken: missing class table");

        (void)i_file_check(TEST_FILE, &statement, &error);
        CHECK(error.type == FILE_CHECK_ERROR_BAD_LAYOUT,
              "broken: invalid metadata layout is reported");
        _write_header(STORAGE_MAGIC, UINT16_MAX);
        (void)i_file_check(TEST_FILE, &statement, &error);
        CHECK(error.type == FILE_CHECK_ERROR_BAD_LAYOUT,
              "broken: malformed count fields are rejected");
}

static void _test_io(void) {
        /* a directory can be opened but not read, or not opened at all */
        _expect("build", ANALYZER_FIND, FILE_CHECK_STATE_FAILED,
                FILE_CHECK_ERROR_IO, "io: directory, read-only statement");
        _expect("build", ANALYZER_NEW, FILE_CHECK_STATE_FAILED,
                FILE_CHECK_ERROR_IO, "io: directory, write statement");
}

static void _test_create_fails(void) {
        const char* path                    = "build/no-such-directory/x.oopdb";
        struct analyzer_statement statement = {ANALYZER_INVALID};
        struct file_check_error error;
        enum FILE_CHECK_STATE got;

        statement.kind = ANALYZER_NEW;
        got            = i_file_check(path, &statement, &error);
        CHECK(got == FILE_CHECK_STATE_FAILED, "create: failure state");
        CHECK(error.type == FILE_CHECK_ERROR_IO && error.os_error != 0,
              "create: failure reports errno");
}

static void _test_path(void) {
        if (getenv("OOPDB") == NULL) {
                CHECK(strcmp(i_file_check_path(), STORAGE_DEFAULT_PATH) == 0,
                      "path: default");
        } else {
                CHECK(strcmp(i_file_check_path(), getenv("OOPDB")) == 0,
                      "path: from the environment");
        }
}

int main(void) {
        _test_missing();
        _test_valid();
        _test_broken();
        _test_io();
        _test_create_fails();
        _test_path();
        remove(TEST_FILE);
        printf("file check tests:\n");
        printf("%d checks, %d failures\n", checks, failures);
        return failures != 0;
}
