/**
 * @file main.c
 * @brief File check implementation, plain C99 stdio (plus the "x" mode flag
 * of fopen, so a file that appears meanwhile is never overwritten).
 */

#include "interface/file-check/main.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "implementation/executor/helper/create_header.h"
#include "implementation/executor/helper/create_metatable.h"
#include "implementation/executor/helper/format_types.h"
#include "implementation/executor/helper/validate_metadata_layout.h"
#include "interface/storage/header.h"

#define FILE_FIXED_SIZE \
        (EXECUTOR_FORMAT_HEADER_SIZE + EXECUTOR_FORMAT_METATABLE_SIZE)

bool i_file_check_statement_is_read_only(enum ANALYZER_STATEMENT kind) {
        return kind == ANALYZER_FIND || kind == ANALYZER_LIST_CLASSES ||
               kind == ANALYZER_LIST_INTERFACES || kind == ANALYZER_DESCRIBE ||
               kind == ANALYZER_CALL;
}

/** Fill in an error. */
static void _fail(struct file_check_error* error,
                  enum file_check_error_type type, int os_error,
                  unsigned long version) {
        error->type     = type;
        error->os_error = os_error;
        error->version  = version;
}

/** Create the file with a fresh header, removes it again if writing fails. */
static bool _create(const char* path, struct file_check_error* error) {
        uint8_t header_bytes[EXECUTOR_FORMAT_HEADER_SIZE];
        uint8_t metatable_bytes[EXECUTOR_FORMAT_METATABLE_SIZE];
        struct executor_format_header header       = {0};
        struct executor_format_metatable metatable = {0};
        FILE* file                                 = fopen(path, "wbx");
        bool failed;
        int saved;

        if (file == NULL) {
                _fail(error, FILE_CHECK_ERROR_IO, errno, 0);
                return false;
        }
        failed = !executor_helper_create_header(header_bytes,
                                                sizeof header_bytes, &header) ||
                 !executor_helper_create_metatable(
                     metatable_bytes, sizeof metatable_bytes, &metatable) ||
                 fwrite(header_bytes, 1, sizeof header_bytes, file) !=
                     sizeof header_bytes ||
                 fwrite(metatable_bytes, 1, sizeof metatable_bytes, file) !=
                     sizeof metatable_bytes;
        saved  = failed ? errno : 0;
        if (fclose(file) != 0 && !failed) {
                failed = true;
                saved  = errno;
        }
        if (failed) {
                remove(path);
                _fail(error, FILE_CHECK_ERROR_IO, saved, 0);
                return false;
        }
        return true;
}

const char* i_file_check_path(void) {
        const char* path = getenv("OOPDB");

        return path != NULL && path[0] != '\0' ? path : STORAGE_DEFAULT_PATH;
}

enum FILE_CHECK_STATE i_file_check(const char* path,
                                   const struct analyzer_statement* statement,
                                   struct file_check_error* error) {
        uint8_t header_bytes[EXECUTOR_FORMAT_HEADER_SIZE];
        uint8_t metatable_bytes[EXECUTOR_FORMAT_METATABLE_SIZE];
        struct executor_format_header header;
        struct executor_format_metatable metatable;
        FILE* file;
        size_t got;
        long file_size;
        int saved;
        bool failed;

        error->path = path;
        _fail(error, FILE_CHECK_OK, 0, 0);

        file = fopen(path, i_file_check_statement_is_read_only(statement->kind)
                               ? "rb"
                               : "r+b");
        if (file == NULL) {
                saved = errno;
                if (saved != ENOENT) {
                        _fail(error, FILE_CHECK_ERROR_IO, saved, 0);
                } else if (i_file_check_statement_is_read_only(
                               statement->kind)) {
                        _fail(error, FILE_CHECK_ERROR_NOT_FOUND, 0, 0);
                } else if (_create(path, error)) {
                        return FILE_CHECK_STATE_CREATED;
                }
                return FILE_CHECK_STATE_FAILED;
        }

        got = fread(header_bytes, 1, sizeof header_bytes, file);
        if (got == sizeof header_bytes) {
                got += fread(metatable_bytes, 1, sizeof metatable_bytes, file);
        }
        saved  = errno;
        failed = ferror(file) != 0;
        if (fseek(file, 0, SEEK_END) != 0) {
                saved  = errno;
                failed = true;
        }
        file_size = failed ? -1 : ftell(file);
        if (file_size < 0) {
                saved  = errno;
                failed = true;
        }
        fclose(file);
        if (got < FILE_FIXED_SIZE) {
                if (failed) {
                        _fail(error, FILE_CHECK_ERROR_IO, saved, 0);
                } else {
                        _fail(error, FILE_CHECK_ERROR_TOO_SHORT, 0, 0);
                }
                return FILE_CHECK_STATE_FAILED;
        }
        if (memcmp(header_bytes, STORAGE_MAGIC, STORAGE_MAGIC_SIZE) != 0) {
                _fail(error, FILE_CHECK_ERROR_BAD_MAGIC, 0, 0);
                return FILE_CHECK_STATE_FAILED;
        }
        if (!executor_helper_read_header(header_bytes, sizeof header_bytes,
                                         &header) ||
            !executor_helper_read_metatable(
                metatable_bytes, sizeof metatable_bytes, &metatable) ||
            (uintmax_t)file_size > (uintmax_t)SIZE_MAX ||
            !executor_helper_validate_metadata_layout((size_t)file_size,
                                                      &header, &metatable)) {
                _fail(error, FILE_CHECK_ERROR_BAD_LAYOUT, 0, 0);
                return FILE_CHECK_STATE_FAILED;
        }
        return FILE_CHECK_STATE_READY;
}
