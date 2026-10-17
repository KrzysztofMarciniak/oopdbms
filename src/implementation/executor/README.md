# Executor

The executor implements the commands listed in the repository-root
`README.md`. It operates on the README-defined offset-based file format, uses
the checked serialization helpers under `helper/`, and commits the header and
metadata locator after each successful mutation.

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

Executor tests live in `src/tests/executor/`. Format tests read the generated
database directly and check bytes against the root README specification.
Run the complete test suite from the repository root:

```sh
make test
```
