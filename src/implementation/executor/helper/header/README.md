
# Header Serialization Helper

Serializes and deserializes the fixed-size database header used by the OOPDB binary format.

## Files

- `main.h` — header structure and public function declarations.
- `main.c` — header serialization and deserialization.

## Binary Format

The database header is exactly 16 bytes.

| Offset | Size | Field | Encoding |
|---:|---:|---|---|
| 0 | 8 | Magic | `OOPDB\0\0\0` |
| 8 | 2 | Interface count | `u16`, little-endian |
| 10 | 2 | Class count | `u16`, little-endian |
| 12 | 2 | Function count | `u16`, little-endian |
| 14 | 2 | Reserved | `u16`, little-endian |

## Public API

- `helper_write_header(FILE *file, const helper_database_header *header)`
- `helper_read_header(FILE *file, helper_database_header *header)`

Both functions return `0` on success and `-1` on failure.

The reader validates the magic bytes and assigns the output structure only after the complete header has been read successfully.

The reserved field is preserved when reading and writing. Callers should initialize it to zero when creating a new database.

## Dependencies

- `helper/basic/` — exact byte-sequence I/O.
- `helper/binary_primitives/` — little-endian `u16` serialization.

## Responsibilities

This module serializes only the database header. It does not open or close files, seek to file offsets, serialize the metatable, or serialize interface, class, function, or object definitions.

The caller is responsible for file positioning and file lifecycle management.

