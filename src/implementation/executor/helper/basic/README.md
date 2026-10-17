# Basic File I/O Helpers

Low-level file I/O helpers used by the OOPDBMS executor.

## Purpose

This module provides generic byte-level read and write operations. It does not interpret database structures, convert byte order, or perform serialization.

Higher-level helpers should use these functions rather than calling `fread()` or `fwrite()` directly.

## Files

- `main.h` — public function declarations.
- `main.c` — function implementations.

## API

### `helper_write_bytes`

```c
int helper_write_bytes(FILE *file, const void *data, size_t length);
```

Writes exactly `length` bytes from `data` to `file`, starting at the current file position.

- Returns `0` on success.
- Returns `-1` if the file is null, the data pointer is null for a nonzero length, or the write fails.
- A zero-length write succeeds without writing anything.

### `helper_read_bytes`

```c
int helper_read_bytes(FILE *file, void *data, size_t length);
```

Reads exactly `length` bytes from `file` into `data`, starting at the current file position.

- Returns `0` on success.
- Returns `-1` if the file is null, the data pointer is null for a nonzero length, or fewer than `length` bytes can be read.
- A zero-length read succeeds without reading anything.

## Usage

```c
#include "helper/basic/main.h"

unsigned char buffer[32] = {0};

if (helper_write_bytes(file, buffer, sizeof(buffer)) != 0) {
    /* Handle the write error. */
}
```

## Responsibilities

This module:

- Reads and writes arbitrary byte sequences.
- Detects unsuccessful or incomplete I/O operations.
- Leaves file positioning under the caller's control.
- Does not open, close, seek, or flush files.
- Does not convert values to a specific byte order.
- Does not allocate or free memory.
- Does not interpret database file structures.

## Dependencies

- C99 standard library: `<stddef.h>`, `<stdio.h>`.
- No external libraries.

## Design rule

All database-specific serialization should be implemented in higher-level helper modules. This module should remain independent of the OOPDBMS file format.
