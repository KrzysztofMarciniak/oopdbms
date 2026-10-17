
# Metatable Serialization Helper

Serializes and deserializes the database metatable in the OOPDB binary format.

## Files

- `main.h` — metatable structures and public function declarations.
- `main.c` — metatable serialization and deserialization.

## Binary Format

The metatable is exactly 48 bytes and contains three entries in this order:

1. Interface table
2. Class table
3. Function table

Each entry is 16 bytes:

| Relative offset | Size | Field | Encoding |
|---:|---:|---|---|
| 0 | 8 | Offset | `u64`, little-endian |
| 8 | 8 | Length | `u64`, little-endian |

## Public API

- `helper_write_metatable(FILE *file, const helper_metatable *metatable)`
- `helper_read_metatable(FILE *file, helper_metatable *metatable)`

Both functions return `0` on success and `-1` on failure.

The reader assigns the output structure only after all six values have been read successfully.

## Dependencies

- `helper/binary_primitives/` — little-endian `u64` serialization.

## Responsibilities

This module serializes only the metatable entries. It does not calculate table offsets or lengths, serialize the tables themselves, validate their contents, or manage file positioning and lifecycle.

The caller is responsible for ensuring that offsets and lengths are consistent with the complete database layout.

