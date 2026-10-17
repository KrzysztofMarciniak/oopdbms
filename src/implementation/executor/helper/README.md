# Executor Serialization Helpers

These helpers own database access for the executor's current class and interface
creation commands. Command implementations pass checked statements to them and
do not allocate pages, update directories, or change database metadata.

## Modules

| Module | Responsibility |
|---|---|
| `basic/` | Read and write raw byte sequences. |
| `binary_primitives/` | Read and write fixed-width numeric primitives in little-endian format. |
| `type_name/` | Convert analyzer types to their stored names. |
| `interface_definition/` | Validate, serialize, and register interfaces; read and write interface definitions. |
| `class_definition/` | Validate, serialize, and register classes; read and write class definitions. |

## Serialization Conventions

### Byte order

All multibyte integers are encoded in little-endian byte order.

Serialized data must follow the defined database format. C structures must not be written directly to the file because their in-memory representation can contain padding and platform-dependent layouts.

### Strings and byte sequences

Names and text values are stored as length-delimited byte sequences. Their serialized representations do not include a terminating NUL byte.

The length fields associated with names and other values determine how many bytes are read or written.

### Type codes

The database format uses the following type codes:

| Code | Type |
|---:|---|
| `1` | Integer |
| `2` | Float |
| `3` | Text |
| `4` | Boolean |
| `5` | Pointer |

Field descriptors used by interface and class definitions contain a field ID, type code, name length, an optional target class ID for pointer fields, and the field name bytes.

## Module Dependencies

The helper modules are layered so that higher-level serialization code can reuse existing primitive operations:

- `basic/` provides exact raw-byte reads and writes.
- `binary_primitives/` uses raw-byte I/O to encode and decode numeric primitives.
- The interface- and class-definition modules use these primitives for their serialized fields.
- The definition modules own the database checks, page allocation, directory updates, and metadata counts for their respective create commands.

Low-level file helpers operate at the current position of an open file. Their
callers are responsible for opening and closing files and seeking as needed.

Read and write functions return `0` on success and `-1` on failure. A failed
write may have already written part of the data, so callers must treat the
output as incomplete.

Functions that allocate memory while deserializing definitions provide
corresponding cleanup functions. Callers must release that memory when it is no
longer needed.

## Include Paths

Project headers use paths relative to the `src/` directory, for example:

```c
#include "implementation/executor/helper/basic/main.h"
#include "implementation/executor/helper/binary_primitives/main.h"
```

The build configuration must include `src/` in the compiler's header search paths.

Class creation checks referenced parent, interface, and pointer-target names
against the database before storing the definition. The serialized definitions
retain those names; they do not resolve them to numeric IDs.
