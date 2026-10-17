
# Function Definition Serialization Helper

Serializes and deserializes function definitions in the OOPDB binary format.

## Binary Format

A function definition consists of these fields, in order:

| Field | Size | Encoding |
|---|---:|---|
| `name_length` | 2 bytes | `u16`, little-endian |
| `parameter_count` | 2 bytes | `u16`, little-endian |
| Return type descriptor | 1 or 5 bytes | See below |
| `expression_length` | 4 bytes | `u32`, little-endian |
| Function name | `name_length` bytes | UTF-8 |
| Parameter descriptors | Variable | See below |
| Expression | `expression_length` bytes | Raw expression bytes |

### Type Descriptor

Every type descriptor starts with a one-byte `type_code`.

For pointer types only, the descriptor additionally contains a 4-byte
`target_class_id` (`u32`, little-endian).

| Type code | Type |
|---:|---|
| 1 | Integer |
| 2 | Float |
| 3 | Text |
| 4 | Bool |
| 5 | Pointer |

### Parameter Descriptor

Each parameter descriptor contains:

| Field | Size | Encoding |
|---|---:|---|
| `name_length` | 2 bytes | `u16`, little-endian |
| Type descriptor | 1 or 5 bytes | See above |
| Parameter name | `name_length` bytes | UTF-8 |

Names are stored without NUL terminators. The expression is a length-delimited
byte sequence and is not NUL-terminated by this module.

## Public API

- `helper_write_function_definition()`
- `helper_read_function_definition()`
- `helper_free_function_definition()`

Read and write functions return `0` on success and `-1` on failure.

The reader allocates the function name, parameter array, parameter names, and
expression bytes. The caller must release them using
`helper_free_function_definition()`.

## Dependencies

- `implementation/executor/helper/basic/` — raw byte I/O.
- `implementation/executor/helper/binary_primitives/` — little-endian
  primitive serialization.

## Responsibilities

This module serializes one function definition. It does not serialize function
table entries, calculate file offsets, parse or execute expressions, resolve
types, or manage file positioning and lifecycle.

