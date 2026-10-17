
# Object Record Serialization Helper

Serializes and deserializes object records in the OOPDB binary format.

## Binary Format

The object record header is exactly 16 bytes.

| Offset | Size | Field | Encoding |
|---:|---:|---|---|
| 0 | 4 | `record_length` | `u32`, little-endian |
| 4 | 8 | `object_id` | `u64`, little-endian |
| 12 | 2 | `flags` | `u16`, little-endian |
| 14 | 2 | `field_count` | `u16`, little-endian |

`record_length` is the size of the complete record, including the 16-byte
header.

Each field record starts with a 9-byte header.

| Relative offset | Size | Field | Encoding |
|---:|---:|---|---|
| 0 | 4 | `field_id` | `u32`, little-endian |
| 4 | 1 | `type_code` | Type code |
| 5 | 4 | `value_length` | `u32`, little-endian |
| 9 | Variable | Value | Depends on type |

## Field Value Encodings

| Type code | Type | Value length | Encoding |
|---:|---|---:|---|
| 1 | Integer | 8 bytes | Signed `i64`, little-endian |
| 2 | Float | 8 bytes | IEEE 754 binary64 |
| 3 | Text | Variable | UTF-8 bytes |
| 4 | Bool | 1 byte | `0` or `1` |
| 5 | Pointer | 12 bytes | `u32` class ID followed by `u64` object ID |

Text values are stored as length-delimited bytes without a NUL terminator.
Pointer values identify both the target class and target object.

## Public API

- `helper_write_object_record()`
- `helper_read_object_record()`
- `helper_free_object_record()`

Read and write functions return `0` on success and `-1` on failure.

The writer calculates `record_length` from the record contents and rejects
records exceeding the `u32` size limit.

The reader validates the declared record size, field boundaries, type codes,
fixed-size value lengths, and boolean values. It rejects records whose fields
do not consume exactly the declared record length.

The reader allocates the field array and text values. The caller must release
those allocations using `helper_free_object_record()`.

## Dependencies

- `implementation/executor/helper/basic/` — raw byte I/O.
- `implementation/executor/helper/binary_primitives/` — little-endian
  primitive serialization.

## Responsibilities

This module serializes one object record. It does not serialize class table
entries or class definitions, resolve pointer references, validate field IDs
against class definitions, manage object storage offsets, or manage file
positioning and lifecycle.

The caller is responsible for opening the file, positioning it correctly,
and managing its lifecycle.

