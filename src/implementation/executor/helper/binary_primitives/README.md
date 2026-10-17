# Binary Primitive Helpers

Binary primitive serialization helpers for the OOPDBMS database file format.

## Purpose

This module serializes and deserializes fixed-width numeric values using explicit little-endian byte order where applicable.

It builds on `helper/basic/` and provides the primitive operations required by database headers, metatable entries, metadata tables, definitions, and object records.

## Files

- `main.h` — public function declarations.
- `main.c` — function implementations.

## Supported types

| Type | Size | Encoding |
|---|---:|---|
| `u8` | 1 byte | Unsigned 8-bit integer |
| `u16` | 2 bytes | Unsigned 16-bit integer, little-endian |
| `u32` | 4 bytes | Unsigned 32-bit integer, little-endian |
| `u64` | 8 bytes | Unsigned 64-bit integer, little-endian |
| `i64` | 8 bytes | Signed 64-bit two's-complement, little-endian |
| `f64` | 8 bytes | IEEE 754 binary64, little-endian |

## API

Each supported type has a corresponding read and write function.

```c
int helper_write_u8(FILE *file, uint8_t value);
int helper_write_u16(FILE *file, uint16_t value);
int helper_write_u32(FILE *file, uint32_t value);
int helper_write_u64(FILE *file, uint64_t value);

int helper_read_u8(FILE *file, uint8_t *value);
int helper_read_u16(FILE *file, uint16_t *value);
int helper_read_u32(FILE *file, uint32_t *value);
int helper_read_u64(FILE *file, uint64_t *value);

int helper_write_i64(FILE *file, int64_t value);
int helper_read_i64(FILE *file, int64_t *value);

int helper_write_f64(FILE *file, double value);
int helper_read_f64(FILE *file, double *value);
```

All functions return `0` on success and `-1` on failure.

Read functions receive a pointer to the destination value. They return an error for a null destination pointer or an unsuccessful read.

## Usage

```c
#include "helper/binary_primitives/main.h"

if (helper_write_u32(file, 42) != 0) {
    /* Handle the write error. */
}

uint32_t value;

if (helper_read_u32(file, &value) != 0) {
    /* Handle the read error. */
}
```

## Byte order

All multibyte integer values are serialized in little-endian order, independently of the host's native byte order.

For example, the value `0x12345678` is written as:

```text
78 56 34 12
```

The implementation must never write raw C structures to disk. Structure padding, host byte order, and platform-specific representations must not affect the database file format.

## Floating-point requirements

The database format defines `float` values as IEEE 754 binary64.

The implementation uses `double` as the host representation and copies its bit pattern before writing it through the unsigned 64-bit serialization helper.

The current size checks alone do not guarantee IEEE 754 compatibility. Supported platforms must provide the expected binary64 representation and byte ordering must be handled consistently.

## Responsibilities

This module:

- Reads and writes fixed-width numeric primitives.
- Implements explicit little-endian integer serialization.
- Encodes signed integers using the database's two's-complement representation.
- Serializes floating-point bit patterns as little-endian 64-bit values.
- Reuses `helper/basic/` for raw byte I/O.

This module does not:

- Serialize strings or variable-length values.
- Serialize type descriptors or pointer references as complete database values.
- Serialize class, interface, or function definitions.
- Serialize object records, headers, or metadata tables.
- Open, close, seek, or flush database files.

## Dependencies

- C99 standard library: `<stdint.h>`, `<stdio.h>`, `<string.h>`, `<limits.h>`.
- `helper/basic/main.h`.
- No external libraries.

## Design rule

Keep this module limited to primitive values. String encoding, type descriptors, field descriptors, metadata entries, and complete database records belong in higher-level serialization helpers.
