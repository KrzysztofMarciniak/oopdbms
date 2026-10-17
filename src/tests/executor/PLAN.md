# Executor Test Plan

## Test anchor

For database-format tests, execute the command, close the database, then read
the `.oopdb` file directly. Check serialized bytes against the format in the
repository-root `README.md`; do not use storage helpers to verify the on-disk
layout. Command-level executor coverage is in `commands.c`. Run all tests with
`make test`.

The format tests are added incrementally. They may fail against the current
page-based implementation until storage and executor code are brought in line
with the README.

## Checklist

### Schema metadata

- [x] Add a raw-byte `CREATE CLASS` test for the header, metatable, class table,
  definition, and fields (`create_class.c`).
- [x] Add a raw-byte `CREATE INTERFACE` test for its table entry, definition,
  and required fields (`create_interface.c`).
- [x] Test that a class definition stores the ID of an implemented interface
  (`create_class_implements_interface.c`).
- [x] Test `parent_class_id` and directly declared fields for an extending
  class (`create_class_extends.c`).
- [x] Make the current schema format tests pass against the README layout.
- [x] Add a pointer-field test for its type code and target class ID
  (`create_class_pointer.c`).
- [x] Exercise every `ALTER CLASS` action, descendant/object field-change
  restrictions, and restrictive class/interface/function/pointer deletion
  behavior in the command-level executor test.
- [x] Add raw-format assertions for schema table counts, offsets, lengths,
  names, and stable IDs after schema mutations
  (`schema_mutations.c`).

### Functions

- [x] Test `CREATE FUNCTION` table entries and definitions: owner class ID,
  name, parameters, return type, and expression (`create_function.c`).
- [x] Test a pointer type descriptor in a function parameter
  (`create_function_pointer.c`).
- [x] Exercise `DROP FUNCTION` after creating and calling a typed function.
- [x] Add raw-format assertions that `DROP FUNCTION` updates the function
  table count, range, and retained entries (`create_function.c`).

### Objects

- [x] Test `NEW` writes a class data segment and a 16-byte object-record header.
- [x] Test object field records for integer, float, text, bool, and pointer
  values, including field ID, type code, value length, and encoded bytes.
- [x] Test `SET` and `DELETE` keep data offsets, segment lengths, and object
  counts consistent.
- [x] Test `FIND` reads records from the class data segment, with and without a
  `WHERE` condition.

### Metadata commands

- [x] Use README-format databases in `list_classes.c` and
  `list_interfaces.c` to exercise metadata-table enumeration, including empty,
  populated, and reopened databases.
- [x] Exercise `DESCRIBE` against the class definition and inherited fields.

## Test boundaries

Assert only behavior and byte layouts specified by `README.md`. In particular,
do not assume a specific initial numeric ID or a display format for listing
commands unless the README defines it.
