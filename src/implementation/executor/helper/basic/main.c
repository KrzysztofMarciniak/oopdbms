#include "main.h"

int helper_write_bytes(FILE* file, const void* data, size_t length) {
        if (file == NULL || (data == NULL && length != 0)) return -1;

        if (length == 0) return 0;

        return fwrite(data, 1, length, file) == length ? 0 : -1;
}

int helper_read_bytes(FILE* file, void* data, size_t length) {
        if (file == NULL || (data == NULL && length != 0)) return -1;

        if (length == 0) return 0;

        return fread(data, 1, length, file) == length ? 0 : -1;
}
