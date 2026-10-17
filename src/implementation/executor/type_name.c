/**
 * @file type_name.c
 * @brief Spell a field type the way the README does, for storage and DESCRIBE.
 */

#include <stdio.h>

#include "implementation/executor/executor_internal.h"

size_t executor_type_name(const struct analyzer_type* type, char* out,
                          size_t out_size) {
        const char* base = NULL;
        int written;

        if (!type || !out || out_size == 0) {
                return 0;
        }

        switch (type->kind) {
                case ANALYZER_TYPE_INTEGER:
                        base = "integer";
                        break;
                case ANALYZER_TYPE_FLOAT:
                        base = "float";
                        break;
                case ANALYZER_TYPE_TEXT:
                        base = "text";
                        break;
                case ANALYZER_TYPE_BOOL:
                        base = "bool";
                        break;
                case ANALYZER_TYPE_POINTER:
                        /* Only a pointer carries a class name. */
                        if (!type->class_name || !type->class_name[0]) {
                                return 0;
                        }
                        written = snprintf(out, out_size, "pointer %s",
                                           type->class_name);
                        return (written > 0 && (size_t)written < out_size)
                                   ? (size_t)written
                                   : 0;
                default:
                        return 0;
        }

        written = snprintf(out, out_size, "%s", base);
        return (written > 0 && (size_t)written < out_size) ? (size_t)written
                                                           : 0;
}
