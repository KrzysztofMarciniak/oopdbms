# Class Definition Serialization Helper

Serializes and deserializes class definitions in the OOPDB binary format.

## Binary Format

The fixed header is 16 bytes:

| Offset | Size | Field | Encoding |
|---:|---:|---|---|
| 0 | 2 | `name_length` | `u16`, little-endian |
| 2 | 2 | `flags` | `u16`, little-endian |
| 4 | 4 | `parent_class_id` | `u32`, little-endian |
| 8 | 4 | `field_count` | `u32`, little-endian |
| 12 | 4 | `interface_count` | `u32`, little-endian |

The header is followed by the class name bytes, all field descriptors, and
then the implemented interface IDs.

Each field descriptor contains:

| Field | Size | Encoding |
|---|---:|---|
| `field_id` | 4 bytes | `u32`, little-endian |
| `type_code` | 1 byte | Type code |
| `name_length` | 2 bytes | `u16`, little-endian |
| `target_class_id` | 4 bytes, pointer fields only | `u32`, little-endian |
| Field name | `name_length` bytes | UTF-8 |

Each implemented interface ID is a 4-byte little-endian `u32`.

Names are stored without NUL terminators.

## Type Codes

- `1` — integer
- `2` — float
- `3` — text
- `4` — bool
- `5` — pointer

## Public API

- `helper_write_class_definition()`
- `helper_read_class_definition()`
- `helper_free_class_definition()`

Read and write functions return `0` on success and `-1` on failure.

The reader allocates the class name, field array, field names, and interface
ID array. The caller must release those allocations with
`helper_free_class_definition()`.

## Dependencies

- `implementation/executor/helper/basic/` — raw byte I/O.
- `implementation/executor/helper/binary_primitives/` — little-endian
  primitive serialization.

## Responsibilities

This module serializes a class definition only. It does not serialize class
table entries or object records, calculate file offsets, resolve parent
classes, validate interface IDs, or manage file positioning and lifecycle.
