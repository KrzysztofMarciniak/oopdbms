# Database Serialization Helpers

This directory contains the low-level serialization helpers used by the OOPDBMS executor to read and write the database binary format.

Each module is responsible for serializing and deserializing a specific component of the database format. The helpers share a common binary representation and build on the raw-byte I/O and primitive serialization modules.

## Directory Structure

| Module | Responsibility |
|---|---|
| `basic/` | Read and write raw byte sequences. |
| `binary_primitives/` | Read and write fixed-width numeric primitives in little-endian format. |
| `header/` | Serialize and deserialize the database header. |
| `metatable/` | Serialize and deserialize the database metatable. |
| `interface_table/` | Serialize and deserialize individual interface table entries. |
| `class_table/` | Serialize and deserialize individual class table entries. |
| `function_table/` | Serialize and deserialize individual function table entries. |
| `interface_definition/` | Serialize and deserialize interface definitions and their field descriptors. |
| `class_definition/` | Serialize and deserialize class definitions, field descriptors, and implemented interface IDs. |
| `function_definition/` | Serialize and deserialize function definitions, parameters, type descriptors, and expression bytes. |
| `object_record/` | Serialize and deserialize object records and their field values. |

## Serialization Conventions

### Byte order

All multibyte integers are encoded in little-endian byte order.

Serialized data must follow the defined database format. C structures must not be written directly to the file because their in-memory representation can contain padding and platform-dependent layouts.

### Strings and byte sequences

Names and text values are stored as length-delimited byte sequences. Their serialized representations do not include a terminating NUL byte.

The length fields associated with names and other values determine how many bytes are read or written.

Function expressions are also represented as length-delimited byte sequences.

### Type codes

The database format uses the following type codes:

| Code | Type | Serialized value representation |
|---:|---|---|
| `1` | Integer | Signed 64-bit integer |
| `2` | Float | IEEE 754 binary64 |
| `3` | Text | Variable-length bytes |
| `4` | Boolean | One byte, `0` or `1` |
| `5` | Pointer | Target class ID (`u32`) and target object ID (`u64`) |

Type descriptors used by function definitions contain a type code and, for pointer types, a target class ID.

Field descriptors used by interface and class definitions contain a field ID, type code, name length, an optional target class ID for pointer fields, and the field name bytes.

## Database Format Components

The fixed-size database structures have the following serialized sizes:

| Component | Serialized size |
|---|---:|
| Database header | 16 bytes |
| Metatable | 48 bytes |
| Interface table entry | 16 bytes |
| Class table entry | 48 bytes |
| Function table entry | 20 bytes |
| Object record header | 16 bytes |
| Object field header | 9 bytes |

Interface definitions, class definitions, function definitions, and complete object records have variable lengths.

The database header begins with the 8-byte magic value `OOPDB\0\0\0`. The metatable contains offset and length pairs for the interface, class, and function tables, in that order.

Offsets refer to absolute byte positions in the database file.

## Module Dependencies

The helper modules are layered so that higher-level serialization code can reuse existing primitive operations:

- `basic/` provides exact raw-byte reads and writes.
- `binary_primitives/` uses raw-byte I/O to encode and decode numeric primitives.
- The database structure modules use `binary_primitives/` for integer fields and `basic/` for variable-length byte sequences.

The table-entry modules serialize individual entries. They do not iterate over complete tables or calculate offsets and lengths for other database components.

The definition modules serialize their respective definitions but do not resolve class references, execute expressions, or manage database layout.

The object-record module serializes field values and pointer references but does not resolve those references against the database contents.

## File I/O Responsibilities

These helpers operate on an already-open `FILE *` at its current position.

Callers are responsible for:

- Opening and closing files.
- Seeking to the appropriate file positions.
- Determining the layout and offsets of complete database sections.
- Managing the number of table entries.
- Validating relationships between definitions and referenced IDs.
- Handling failures and ensuring that incomplete writes are not treated as successful database serialization.

Read and write functions return `0` on success and `-1` on failure. A failed write may have already written part of the data, so callers must treat the output as incomplete.

Functions that allocate memory while deserializing definitions or object records provide corresponding cleanup functions. Callers must release the allocated memory when it is no longer needed.

## Include Paths

Project headers use paths relative to the `src/` directory, for example:

```c
#include "implementation/executor/helper/basic/main.h"
#include "implementation/executor/helper/binary_primitives/main.h"
```

The build configuration must include `src/` in the compiler's header search paths.

## Scope

This directory implements the serialization of individual database format components. It does not implement the complete database storage engine, query execution, schema validation, object lookup, index management, or transaction management.

The helpers should be compiled and tested together against the specified binary format, including round-trip tests and malformed or truncated input cases.
