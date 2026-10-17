#include "error/file-check/main.h"

#include <stdio.h>
#include <string.h>

#include "interface/storage/header.h"

void file_check_error_print(struct file_check_error fe) {
        switch (fe.type) {
                case FILE_CHECK_OK:
                        puts("file check: OK");
                        break;
                case FILE_CHECK_ERROR_NOT_FOUND:
                        fprintf(stderr,
                                "file check: %s not found, nothing to read "
                                "(a statement that writes creates it)\n",
                                fe.path);
                        break;
                case FILE_CHECK_ERROR_IO:
                        fprintf(stderr, "file check: cannot use %s: %s\n",
                                fe.path, strerror(fe.os_error));
                        break;
                case FILE_CHECK_ERROR_TOO_SHORT:
                        fprintf(stderr,
                                "file check: %s is too short to be a "
                                "database\n",
                                fe.path);
                        break;
                case FILE_CHECK_ERROR_BAD_MAGIC:
                        fprintf(stderr, "file check: %s is not an oopdb file\n",
                                fe.path);
                        break;
                case FILE_CHECK_ERROR_BAD_VERSION:
                        fprintf(stderr,
                                "file check: %s has format version %lu, this "
                                "build reads version %lu\n",
                                fe.path, fe.version, STORAGE_VERSION);
                        break;
                case FILE_CHECK_ERROR_BAD_LAYOUT:
                        fprintf(stderr,
                                "file check: %s has invalid database layout\n",
                                fe.path);
                        break;
                default:
                        puts("Unknown file check error");
                        break;
        }
}
