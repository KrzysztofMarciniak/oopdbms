#ifndef ERROR_FILE_CHECK_MAIN_H
#define ERROR_FILE_CHECK_MAIN_H

/**
 * @file main.h
 * @brief File check errors.
 */

/**
 * @brief What is wrong with the database file.
 */
enum file_check_error_type {
        /** Nothing is wrong. */
        FILE_CHECK_OK = 0,
        /** The file does not exist and the statement only reads. */
        FILE_CHECK_ERROR_NOT_FOUND,
        /** The file cannot be opened, read or created, see os_error. */
        FILE_CHECK_ERROR_IO,
        /** The file is shorter than a header. */
        FILE_CHECK_ERROR_TOO_SHORT,
        /** The file does not start with the oopdb magic. */
        FILE_CHECK_ERROR_BAD_MAGIC,
        /** The file has a format version this build does not read. */
        FILE_CHECK_ERROR_BAD_VERSION,
        /** The header, tables, or metadata ranges do not match the format. */
        FILE_CHECK_ERROR_BAD_LAYOUT
};

/**
 * @brief File check result.
 */
struct file_check_error {
        /** What is wrong, FILE_CHECK_OK if nothing. */
        enum file_check_error_type type;
        /** The path that was checked, borrowed from the caller. */
        const char* path;
        /** errno for FILE_CHECK_ERROR_IO. */
        int os_error;
        /** Version found in the file for FILE_CHECK_ERROR_BAD_VERSION. */
        unsigned long version;
};

/**
 * @brief Print a file check error.
 *
 * @param fe Error to print.
 */
void file_check_error_print(struct file_check_error fe);

#endif
