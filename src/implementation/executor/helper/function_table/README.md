
# Function Table Serialization Helper

Serializes and deserializes individual function table entries in the OOPDB binary format.

## Binary Format

Each entry is exactly 20 bytes.

| Offset | Size | Field | Encoding |
|---:|---:|---|---|
| 0 | 4 | `function_id` | `u32`, little-endian |
| 4 | 4 | `owner_class_id` | `u32`, little-endian |
| 8 | 8 | `definition_offset` | `u64`, little-endian |
| 16 | 4 | `definition_length` | `u32`, little-endian |

## Public API

- `helper_write_function_table_entry(FILE *file, const helper_function_table_entry *entry)`
- `helper_read_function_table_entry(FILE *file, helper_function_table_entry *entry)`

Both functions return `0` on success and `-1` on failure.

## Dependencies

- `helper/binary_primitives/` — little-endian integer serialization.

## Responsibilities

This module serializes one function table entry at the current file position. The caller manages iteration, table sizing, offset calculations, file positioning, and file lifecycle.

It does not serialize function definitions or validate referenced file ranges.

