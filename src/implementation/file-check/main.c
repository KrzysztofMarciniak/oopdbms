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

#include "interface/storage/header.h"

/** Statements that never write to the file. */
static bool _is_read_only(enum ANALYZER_STATEMENT kind) {
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

/** Read the little-endian u32 at bytes[0..3]. */
static unsigned long _read_u32(const unsigned char* bytes) {
        return (unsigned long)bytes[0] | ((unsigned long)bytes[1] << 8) |
               ((unsigned long)bytes[2] << 16) |
               ((unsigned long)bytes[3] << 24);
}

/** Write v as a little-endian u32 to bytes[0..3]. */
static void _write_u32(unsigned char* bytes, unsigned long v) {
        bytes[0] = (unsigned char)(v & 0xff);
        bytes[1] = (unsigned char)((v >> 8) & 0xff);
        bytes[2] = (unsigned char)((v >> 16) & 0xff);
        bytes[3] = (unsigned char)((v >> 24) & 0xff);
}

/** Create the file with a fresh header, removes it again if writing fails. */
static bool _create(const char* path, struct file_check_error* error) {
        unsigned char header[STORAGE_HEADER_SIZE];
        FILE* file = fopen(path, "wbx");
        bool failed;
        int saved;

        if (file == NULL) {
                _fail(error, FILE_CHECK_ERROR_IO, errno, 0);
                return false;
        }
        memcpy(header, STORAGE_MAGIC, STORAGE_MAGIC_SIZE);
        _write_u32(header + STORAGE_MAGIC_SIZE, STORAGE_VERSION);
        failed = fwrite(header, 1, sizeof header, file) != sizeof header;
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
        unsigned char header[STORAGE_HEADER_SIZE];
        unsigned long version;
        FILE* file;
        size_t got;
        int saved;
        bool failed;

        error->path = path;
        _fail(error, FILE_CHECK_OK, 0, 0);

        file = fopen(path, _is_read_only(statement->kind) ? "rb" : "r+b");
        if (file == NULL) {
                saved = errno;
                if (saved != ENOENT) {
                        _fail(error, FILE_CHECK_ERROR_IO, saved, 0);
                } else if (_is_read_only(statement->kind)) {
                        _fail(error, FILE_CHECK_ERROR_NOT_FOUND, 0, 0);
                } else if (_create(path, error)) {
                        return FILE_CHECK_STATE_CREATED;
                }
                return FILE_CHECK_STATE_FAILED;
        }

        got    = fread(header, 1, sizeof header, file);
        saved  = errno;
        failed = ferror(file) != 0;
        fclose(file);
        if (got < sizeof header) {
                if (failed) {
                        _fail(error, FILE_CHECK_ERROR_IO, saved, 0);
                } else {
                        _fail(error, FILE_CHECK_ERROR_TOO_SHORT, 0, 0);
                }
                return FILE_CHECK_STATE_FAILED;
        }
        if (memcmp(header, STORAGE_MAGIC, STORAGE_MAGIC_SIZE) != 0) {
                _fail(error, FILE_CHECK_ERROR_BAD_MAGIC, 0, 0);
                return FILE_CHECK_STATE_FAILED;
        }
        version = _read_u32(header + STORAGE_MAGIC_SIZE);
        if (version != STORAGE_VERSION) {
                _fail(error, FILE_CHECK_ERROR_BAD_VERSION, 0, version);
                return FILE_CHECK_STATE_FAILED;
        }
        return FILE_CHECK_STATE_READY;
}
