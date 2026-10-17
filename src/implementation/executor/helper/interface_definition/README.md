
# Interface Definition Serialization Helper

Serializes and deserializes interface definitions in the OOPDB binary format.

## Binary Format

An interface definition consists of:

| Field | Size | Encoding |
|---|---:|---|
| `name_length` | 2 bytes | `u16`, little-endian |
| `field_count` | 2 bytes | `u16`, little-endian |
| `name` | `name_length` bytes | UTF-8 |
| Field descriptors | Variable | See below |

Each field descriptor contains:

| Field | Size | Encoding |
|---|---:|---|
| `field_id` | 4 bytes | `u32`, little-endian |
| `type_code` | 1 byte | Type code |
| `name_length` | 2 bytes | `u16`, little-endian |
| `target_class_id` | 4 bytes, pointer fields only | `u32`, little-endian |
| `name` | `name_length` bytes | UTF-8 |

Type codes:

- `1` — integer
- `2` — float
- `3` — text
- `4` — bool
- `5` — pointer

Names are stored without NUL terminators.

## Public API

- `helper_write_interface_definition()`
- `helper_read_interface_definition()`
- `helper_free_interface_definition()`

Read and write functions return `0` on success and `-1` on failure.

The reader allocates the definition name, field array, and field names. The caller must release that memory using `helper_free_interface_definition()`.

## Dependencies

- `helper/basic/` — raw byte I/O.
- `helper/binary_primitives/` — little-endian integer serialization.

## Responsibilities

This module serializes interface definitions only. It does not serialize interface table entries, calculate file offsets, or manage file positioning and lifecycle.

