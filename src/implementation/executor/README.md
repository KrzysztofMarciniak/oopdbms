# Executor

The executor implements the commands listed in the repository-root
`README.md`. It operates on the README-defined offset-based file format, uses
the checked serialization helpers under `helper/`, and commits the header and
metadata locator after each successful mutation.

## Implementation layout

- `executor.c` validates and dispatches statements, then saves a modified
  database.
- `executor_storage.c` contains common format-file lookups, identifier
  allocation, and table-writing helpers. `executor_schema_support.c` owns
  class and interface views, inherited-schema handling, and schema constraints.
- `executor_schema.c` implements class and interface mutations;
  `executor_objects.c` implements object creation, lookup, update, and deletion.
- `executor_function_definitions.c` handles function definitions and
  expression validation; `executor_expression.c` parses and evaluates
  expressions; `executor_function_call.c` loads and executes functions.
- `executor_metadata.c` implements listings and class descriptions.

## Supported commands

- Schema: `CREATE CLASS`, `DROP CLASS`, all documented `ALTER CLASS` actions,
  `CREATE INTERFACE`, and `DROP INTERFACE`.
- Functions: `CREATE FUNCTION`, `DROP FUNCTION`, and `CALL`.
- Objects: `NEW`, `FIND` with or without `WHERE`, `SET`, and `DELETE`.
- Metadata: `LIST CLASSES`, `LIST INTERFACES`, and `DESCRIBE`.

Function expressions and `FIND WHERE` expressions use the documented
precedence and operators. Function definitions are checked against the
declaring class's fields, parameters, and return type when created, then
evaluated against the selected object's values when called.

## Referential and schema constraints

The executor rejects operations that would leave dangling or invalid
references; it does not cascade deletions. A class cannot be dropped while it
has objects, subclasses, pointer fields referring to it, or functions using
it. An interface cannot be dropped while classes implement it. An object
cannot be deleted while a pointer field refers to it.

Field changes are rejected when the affected class or a descendant has stored
objects. Adding or renaming a field is also rejected if it would duplicate a
field declared by a descendant. A class must satisfy all fields and types of
each interface it implements, including fields inherited from its parent.
Field IDs and class, interface, and function IDs are kept stable across
renames.

The README does not prescribe command output formatting. Listings print one
name per line; `FIND` prints each matching object and its values, `CALL` prints
the returned value, and `DESCRIBE` prints the class, inherited fields, and
implemented interfaces.

## Testing

Tests live in `src/tests/` and run together through one terminal dashboard.
Format tests read the generated database directly and check bytes against the
root README specification. Run the complete test suite from the repository
root:

```sh
make test
```

To list available suites or run one suite, use:

```sh
make test TEST_ARGS=--list
make test TEST_ARGS="--suite analyzer"
```

The runner displays colored status in interactive terminals and plain text
when output is redirected or `NO_COLOR` is set. It returns a non-zero status if
any selected suite fails.
