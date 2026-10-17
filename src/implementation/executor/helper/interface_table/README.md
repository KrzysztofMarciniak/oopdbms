
# Interface Table Serialization Helper

Serializes and deserializes individual interface table entries in the OOPDB binary format.

## Binary Format

Each entry is exactly 16 bytes.

| Offset | Size | Field | Encoding |
|---:|---:|---|---|
| 0 | 4 | `interface_id` | `u32`, little-endian |
| 4 | 8 | `definition_offset` | `u64`, little-endian |
| 12 | 4 | `definition_length` | `u32`, little-endian |

## Public API

- `helper_write_interface_table_entry(FILE *file, const helper_interface_table_entry *entry)`
- `helper_read_interface_table_entry(FILE *file, helper_interface_table_entry *entry)`

Both functions return `0` on success and `-1` on failure.

## Dependencies

- `helper/binary_primitives/` — little-endian integer serialization.

## Responsibilities

This module serializes one interface table entry at the current file position. The caller is responsible for iterating over entries, determining the number of entries, calculating offsets and lengths, and managing file positioning and lifecycle.

The module does not serialize interface definitions or validate referenced offsets against the database file.

