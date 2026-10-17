
# Class Table Serialization Helper

Serializes and deserializes individual class table entries in the OOPDB binary format.

## Binary Format

Each entry is exactly 48 bytes.

| Offset | Size | Field | Encoding |
|---:|---:|---|---|
| 0 | 4 | `class_id` | `u32`, little-endian |
| 4 | 8 | `definition_offset` | `u64`, little-endian |
| 12 | 4 | `definition_length` | `u32`, little-endian |
| 16 | 8 | `data_offset` | `u64`, little-endian |
| 24 | 8 | `data_length` | `u64`, little-endian |
| 32 | 8 | `object_count` | `u64`, little-endian |
| 40 | 8 | `next_object_id` | `u64`, little-endian |

## Public API

- `helper_write_class_table_entry(FILE *file, const helper_class_table_entry *entry)`
- `helper_read_class_table_entry(FILE *file, helper_class_table_entry *entry)`

Both functions return `0` on success and `-1` on failure.

## Dependencies

- `helper/binary_primitives/` — little-endian integer serialization.

## Responsibilities

This module serializes one class table entry at the current file position. The caller manages iteration, table sizing, offset calculations, file positioning, and file lifecycle.

It does not serialize class definitions or object records, or validate referenced file ranges.

