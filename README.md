# oopdbms

An object-oriented programming-focused database management system.

## Quick start

Build an optimized binary, create a fresh demo database, and follow a guided
walkthrough of interfaces, inheritance, pointers, objects, filters, functions,
updates, and metadata:

```sh
make quick_start
```

The binary is `build/release/oopdbms`; the demo database is
`build/quick-start.oopdb`. Each run recreates that database. Set
`QUICK_START_DB` to choose another path. For a system-wide or user-local
installation, use `make install` or `make install PREFIX="$HOME/.local"`.

## Commands

### Classes

```text
CREATE CLASS <name> [EXTENDS <class>] [IMPLEMENTS <interface>[, ...]] (<field> <type>[, ...])
DROP CLASS <name>
ALTER CLASS <name> ADD FIELD <field> <type>
ALTER CLASS <name> DROP FIELD <field>
ALTER CLASS <name> RENAME FIELD <old> <new>
ALTER CLASS <name> RENAME <new>
ALTER CLASS <name> EXTENDS <class>
ALTER CLASS <name> IMPLEMENTS <interface>
```

### Interfaces

```text
CREATE INTERFACE <name> (<field> <type>[, ...])
DROP INTERFACE <name>
```

### Functions

```text
CREATE FUNCTION <class>.<name> (<arg> <type>[, ...]) RETURNS <type> AS <expression>
DROP FUNCTION <class>.<name>
CALL <class>.<name> (<value>[, ...]) ON <id>
```

### Objects

```text
NEW <class> (<field> = <value>[, ...])
FIND <class> [WHERE <condition>]
SET <class> <id> (<field> = <value>[, ...])
DELETE <class> <id>
```

### Meta

```text
LIST CLASSES
LIST INTERFACES
DESCRIBE <class>
```

### Types

```text
integer
float
text
bool
pointer <class>
```

`pointer <class>` represents a reference to an object of the specified class, similar to a SQL foreign key. Its command-line value is written as `<class>.<id>`, for example `Person.1`.

## Examples

### Classes and interfaces

```text
CREATE INTERFACE Named (name text)
CREATE CLASS Person IMPLEMENTS Named (name text, age integer)
CREATE CLASS Employee EXTENDS Person (salary float, active bool)
CREATE CLASS Book (title text, price float, author pointer Person)

ALTER CLASS Book ADD FIELD pages integer
ALTER CLASS Book RENAME FIELD pages page_count
ALTER CLASS Book DROP FIELD page_count
ALTER CLASS Book RENAME Novel
ALTER CLASS Employee IMPLEMENTS Named

DROP CLASS Novel
DROP INTERFACE Named
```

### Functions

```text
CREATE FUNCTION Employee.raised (percent float) RETURNS float AS salary * (1 + percent / 100)
CALL Employee.raised (10) ON 7
DROP FUNCTION Employee.raised
```

### Objects

```text
NEW Person (name = "Frank Herbert", age = 65)
NEW Book (title = "Dune", price = 9.99, author = Person.1)

FIND Person
FIND Book WHERE price < 20
FIND Employee WHERE active = true

SET Book 1 (price = 12.50)
DELETE Book 1
```

### Meta

```text
LIST CLASSES
LIST INTERFACES
DESCRIBE Employee
```

## Building

```sh
make                         # Format, then build to build/oopdbms
make run                     # Build and run
make test                    # Build and run every test in src/tests/
make release                 # Optimized build to build/release/oopdbms
make install                 # Install release binary to /usr/local/bin
make install PREFIX="$HOME/.local" # Install to ~/.local/bin without root
make docs                    # Generate Doxygen HTML at build/docs/html/index.html
make format                  # Run clang-format on all sources
make clean                   # Remove build/
```

`make test TEST_ARGS=--list` lists the available test suites. Run one suite
with `make test TEST_ARGS="--suite analyzer"`. The unified terminal dashboard
groups results, reports pass/fail totals, and uses plain text when redirected
or when `NO_COLOR` is set.

The project builds with C99 and Make; `make`, `make release`, and
`make quick_start` also run `clang-format`. `make install` requires the
`install` utility, and `make docs` additionally requires Doxygen.

## How a command runs

The entire command line is one statement. It can be passed as one quoted argument or as several arguments:

```sh
oopdbms 'CREATE CLASS Person (name text, age integer)'
oopdbms CREATE CLASS Person '(name text, age integer)'
```

A statement passes through these steps. If a step fails, it prints an error to standard error and exits with status `1`.

```text
1. tokenizer    argv -> tokens
2. analyzer     tokens -> checked statement (syntax only)
3. file check   verify that the database file can be used
4. execute      execute the statement (supported commands are tracked in
                src/implementation/executor/README.md)
```

Each step has an interface under `src/interface/<step>/`, an implementation under `src/implementation/<step>/`, and error definitions/printing under `src/error/<step>/`.

### Tokenizer

The tokenizer skips `argv[0]`, treats the remaining arguments like a small shell input, and classifies the pieces.

```text
words        [A-Za-z_][A-Za-z0-9_]*
numbers      42   9.99   (a sign is a separate - token)
text         "Frank Herbert" or 'Dune'; supports \" and \\ escapes
operators    =  !=  <  <=  >  >=  +  -  *  /
punctuation  (  )  ,  .   (split even without spaces, e.g. Person.1)
```

Keywords are case-sensitive: commands are uppercase; types and `true`/`false` are lowercase.

The tokenizer recognizes these two-word tokens even when the words are separate arguments:

```text
CREATE CLASS       DROP CLASS        ALTER CLASS
CREATE INTERFACE   DROP INTERFACE    CREATE FUNCTION
DROP FUNCTION      LIST CLASSES      LIST INTERFACES
ADD FIELD          DROP FIELD        RENAME FIELD
```

`ALTER CLASS` is followed by a class name; the action after that name (`ADD FIELD`, `DROP FIELD`, `RENAME FIELD`, `RENAME`, `EXTENDS`, or `IMPLEMENTS`) is parsed separately.

Every token carries its type (`enum TOKENIZER_TOKEN`) and source position (argument and offset). Names, text literals, and numbers own copies of their values. Text literals are stored without their quotes and with escapes decoded. Numeric strings are not converted to numeric values by the tokenizer.

Tokenizer errors include illegal characters (such as `@` or a lone `!`), unterminated text, malformed numbers (such as `12abc`), and incomplete commands (such as `CREATE Person`).

### Analyzer

The analyzer checks the command grammar and builds one `struct analyzer_statement` that owns its strings. It checks syntax only; it does not check whether a class exists or whether a value matches a stored field type.

The expression grammar is:

```text
expression  additive [comparison additive]
additive    term {(+ | -) term}
term        unary {(* | /) unary}
unary       - unary | primary
primary     <name> | <literal> | ( additive )
condition   additive comparison additive   (WHERE requires a comparison)
value       <literal> | -<number> | <class>.<id>
```

`*` and `/` bind more tightly than `+` and `-`. Chains are left-associative. Field lists, argument lists, `CALL` values, and assignment lists require at least one entry. Parentheses and unary minus may nest up to 128 levels.

Analyzer errors include the offending token's source spelling and, where useful,
the alternatives accepted at that point. Long token spellings are truncated in
the diagnostic; locations continue to use the token's argument index and byte
offset. For example:

```text
analyzer: expected ')' but found end of input
analyzer: unexpected 'Person' (a name); expected a command (CREATE, DROP, ALTER, CALL, NEW, FIND, SET, DELETE, LIST, or DESCRIBE) at argument 1, offset 0
analyzer: unexpected 'unknown' (a name); expected a type (integer, float, text, bool, or pointer) at argument 1, offset 25
```

### File check

The file check determines whether the database file can be used for a statement and creates it when required.

```text
FIND, LIST, DESCRIBE, CALL   read-only: the file must already exist;
                             a missing file is an error and is not created
other commands               write: create a missing file with a valid header;
                             an existing file must be writable
```

The default path is `database.oopdb` in the current directory. Set `OOPDB` to use another path:

```sh
OOPDB=/srv/books.oopdb oopdbms FIND Book
```

The file check validates the database magic, header, metatable, and metadata
table ranges, including their lengths, file bounds, and overlap. Errors include
a missing file, failure to open/read/create a file (including the operating
system reason), a file that is too short, an incorrect magic value, and an
invalid database layout.

## Source layout

```text
src/main.c                         Runs the steps in order
src/interface/<name>/              Public interface headers for a step
src/implementation/<name>/         Implementation of a step
src/implementation/executor/       Focused executor modules; see its README
src/error/<name>/                  Error types and printing
src/tests/test_runner.c            Unified terminal test runner
src/tests/<name>/*.c               Test suites for a step
```

Test suites print `N checks, M failures`; `make test` exits non-zero if any
suite fails.

## Database file format

The file is divided into a fixed header, a metatable, metadata tables and definitions, and class data segments. The header and metatable locate the metadata; metadata entries describe classes, interfaces and functions and point to their definitions and, for classes, their data segments.

All multi-byte integers are stored in little-endian byte order. Fields are serialized individually in the defined order; do not write raw C structs directly to disk, because C struct padding and host endianness are implementation-dependent. Offsets are absolute byte offsets measured from the beginning of the file. The layouts below are packed byte layouts: no implicit padding is inserted between fields.

### Header

The header is 16 bytes:

| Offset | Size | Field | Description |
|---:|---:|---|---|
| 0 | 8 | `magic` | ASCII `OOPDB` followed by three zero bytes: `4f 4f 50 44 42 00 00 00` |
| 8 | 2 | `interface_count` | Number of interface table entries (`u16`) |
| 10 | 2 | `class_count` | Number of class table entries (`u16`) |
| 12 | 2 | `function_count` | Number of function table entries (`u16`) |
| 14 | 2 | `reserved` | Reserved; write as zero (`u16`) |

The three counts are each unsigned 16-bit integers, so each metadata table can contain at most 65,535 entries. The reserved field is included for future use and must be zero in this layout.

### Metatable

The metatable immediately follows the header and is 48 bytes long. It contains three 16-byte entries in this fixed order: interfaces, classes, functions.

Each entry consists of:

| Field | Size | Description |
|---|---:|---|
| `offset` | 8 | Absolute byte offset of the corresponding metadata table (`u64`) |
| `length` | 8 | Length of that table in bytes (`u64`) |

```text
Metatable (48 bytes)
+------------------------------+
| Interfaces: offset, length   | 16 bytes
+------------------------------+
| Classes: offset, length      | 16 bytes
+------------------------------+
| Functions: offset, length    | 16 bytes
+------------------------------+
```

The table lengths must agree with the entry count in the header and the defined size of the corresponding table entry. For example, a class table containing `class_count` entries has a length of `class_count * 48` bytes.

### Interface table

Each interface table entry is 16 bytes:

| Field | Size | Description |
|---|---:|---|
| `interface_id` | 4 | Stable interface identifier (`u32`) |
| `definition_offset` | 8 | Absolute offset of the interface definition (`u64`) |
| `definition_length` | 4 | Length of the definition in bytes (`u32`) |

An interface definition contains:

| Field | Size | Description |
|---|---:|---|
| `name_length` | 2 | Name length in bytes (`u16`) |
| `field_count` | 2 | Number of required fields (`u16`) |
| `name` | `name_length` | UTF-8 bytes; no terminating NUL |
| `fields` | Variable | Field descriptors, as defined below |

### Class table

Each class table entry is 48 bytes:

| Field | Size | Description |
|---|---:|---|
| `class_id` | 4 | Stable class identifier (`u32`) |
| `definition_offset` | 8 | Absolute offset of the class definition (`u64`) |
| `definition_length` | 4 | Length of the class definition (`u32`) |
| `data_offset` | 8 | Absolute offset of the class data segment (`u64`) |
| `data_length` | 8 | Length of the class data segment in bytes (`u64`) |
| `object_count` | 8 | Number of live objects in the class (`u64`) |
| `next_object_id` | 8 | Next object ID to allocate for this class (`u64`) |

A class definition has this layout:

| Field | Size | Description |
|---|---:|---|
| `name_length` | 2 | Class-name length in bytes (`u16`) |
| `flags` | 2 | Reserved class flags (`u16`); zero when unused |
| `parent_class_id` | 4 | Parent class ID (`u32`); zero means no parent |
| `field_count` | 4 | Number of fields declared by this class (`u32`) |
| `interface_count` | 4 | Number of implemented interfaces (`u32`) |
| `name` | `name_length` | UTF-8 bytes; no terminating NUL |
| `fields` | Variable | Field descriptors for fields declared by this class |
| `interface_ids` | `interface_count * 4` | Implemented interface IDs (`u32` each) |

Only fields declared directly on a class are stored in its definition. Inherited fields are resolved by walking the parent-class chain. Renaming a class or field changes schema metadata; stable numeric IDs identify entities independently of their names. Field IDs must be unique throughout the database.

### Field descriptors and type descriptors

A field descriptor appears in class and interface definitions. It is variable-length:

| Field | Size | Description |
|---|---:|---|
| `field_id` | 4 | Stable field identifier (`u32`) |
| `type_code` | 1 | Type code from the table below (`u8`) |
| `name_length` | 2 | Field-name length in bytes (`u16`) |
| `target_class_id` | 4, pointer fields only | Target class ID (`u32`); present only when `type_code` is `pointer` |
| `name` | `name_length` | UTF-8 field-name bytes; no terminating NUL |

Type codes are:

| Code | Type | Stored value |
|---:|---|---|
| 1 | `integer` | Signed 64-bit integer (`i64`) |
| 2 | `float` | IEEE 754 binary64 (`f64`) |
| 3 | `text` | UTF-8 bytes |
| 4 | `bool` | One byte: `0` for false, `1` for true |
| 5 | `pointer` | Target class ID (`u32`) followed by target object ID (`u64`) |

A type descriptor is a `type_code` byte and, only for `pointer`, a `target_class_id` (`u32`). The `target_class_id` in a pointer field definition specifies the class of objects that field may reference.

### Function table

Each function table entry is 20 bytes:

| Field | Size | Description |
|---|---:|---|
| `function_id` | 4 | Stable function identifier (`u32`) |
| `owner_class_id` | 4 | ID of the class that owns the function (`u32`) |
| `definition_offset` | 8 | Absolute offset of the function definition (`u64`) |
| `definition_length` | 4 | Length of the function definition (`u32`) |

A function definition contains, in order:

| Field | Size | Description |
|---|---:|---|
| `name_length` | 2 | Function-name length in bytes (`u16`) |
| `parameter_count` | 2 | Number of parameters (`u16`) |
| `return_type` | 1 or 5 | Type descriptor: one byte, plus target class ID for `pointer` |
| `expression_length` | 4 | Expression length in bytes (`u32`) |
| `name` | `name_length` | UTF-8 function-name bytes; no terminating NUL |
| `parameters` | Variable | Parameter descriptors, as described below |
| `expression` | `expression_length` | UTF-8 expression bytes; no terminating NUL |

Each parameter descriptor contains `name_length` (`u16`), a type descriptor (one byte, plus a `u32` target class ID for `pointer`), then `name_length` bytes containing the UTF-8 parameter name. Function expressions are stored as source text, such as `salary * (1 + percent / 100)`.

### Class data segments

Class data segments follow the metadata definitions. A class table entry's `data_offset` and `data_length` identify its segment. The segment contains object records belonging to that class.

Each object record begins with a 16-byte header:

| Field | Size | Description |
|---|---:|---|
| `record_length` | 4 | Total record length, including this header (`u32`) |
| `object_id` | 8 | Object ID, unique within its class (`u64`) |
| `flags` | 2 | Object flags (`u16`); zero when unused |
| `field_count` | 2 | Number of field records in the object (`u16`) |

The object header is followed by `field_count` field records. Each field record contains:

| Field | Size | Description |
|---|---:|---|
| `field_id` | 4 | ID of the field described by this record (`u32`) |
| `type_code` | 1 | Type code from the type-code table (`u8`) |
| `value_length` | 4 | Length of the value in bytes (`u32`) |
| `value` | `value_length` | Encoded value bytes |

Values use the encodings in the type-code table. For a pointer value, the value is a 4-byte target class ID followed by an 8-byte target object ID. For text, `value_length` counts UTF-8 bytes and no terminating NUL is stored. The type of a field is defined by the class schema; a stored field's type code must match that schema.

Object IDs are scoped to their class, which matches command values such as `Book.1`. A pointer stores both the target class ID and object ID; it does not store a file offset or the text form of the reference.

### Physical arrangement

A convenient physical arrangement is:

```text
+----------------------------------+
| Header (16 bytes)                |
+----------------------------------+
| Metatable (48 bytes)             |
+----------------------------------+
| Interface table                  |
+----------------------------------+
| Class table                      |
+----------------------------------+
| Function table                   |
+----------------------------------+
| Interface definitions            |
+----------------------------------+
| Class definitions                |
+----------------------------------+
| Function definitions             |
+----------------------------------+
| Class data segment: first class  |
+----------------------------------+
| Class data segment: next class   |
+----------------------------------+
| ...                              |
+----------------------------------+
```

This is the recommended ordering, not a substitute for following the offsets recorded in the metatable and metadata tables. Sections must not overlap, and every offset/length pair must fit entirely within the file. Readers must validate lengths before allocating memory or seeking. Text is length-prefixed UTF-8, not NUL-terminated.

A class's data segment contains its current object records. When a mutation changes a variable-length record or segment, the implementation must keep the segment lengths and offsets consistent with the class table. An object index or secondary field indexes may be added later; they are not required by this layout, so `FIND` can initially scan the relevant class data segment.
