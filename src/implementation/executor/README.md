# Executor

The executor applies analyzer-checked statements to the open database.
`executor.c` dispatches each statement by command group and kind to its
command-specific implementation. Successful mutations are saved by the
dispatcher.

## Progress

- [x] `CREATE CLASS` — stores a class definition, including optional parent
  and implemented-interface names.
- [ ] `DROP CLASS`
- [ ] `ALTER CLASS ... ADD FIELD`
- [ ] `ALTER CLASS ... DROP FIELD`
- [ ] `ALTER CLASS ... RENAME FIELD`
- [ ] `ALTER CLASS ... RENAME`
- [ ] `ALTER CLASS ... EXTENDS`
- [ ] `ALTER CLASS ... IMPLEMENTS`
- [x] `CREATE INTERFACE` — stores an interface definition.
- [ ] `DROP INTERFACE`
- [ ] `CREATE FUNCTION`
- [ ] `DROP FUNCTION`
- [ ] `CALL`
- [ ] `NEW`
- [ ] `FIND`
- [ ] `SET`
- [ ] `DELETE`
- [x] `LIST CLASSES` — reads names from the class-directory page chain.
- [x] `LIST INTERFACES` — reads names from the interface-directory page chain.
- [ ] `DESCRIBE`

### Next

Implement `NEW` to start the object-data path needed by `FIND`, `SET`, and
`DELETE`.

## Implementation notes

- Each command has a dedicated `.c` file; declarations and command contracts
  are in `executor_internal.h`.
- Commands not checked above currently return
  `EXECUTOR_ERROR_NOT_IMPLEMENTED`.
- Directory entry serialization and page-chain traversal are handled by the
  storage helpers in `interface/storage/directory.h` and
  `implementation/storage/database.c`.
- Schema definitions are encoded with the page-based helpers under
  `helper/class_definition/` and `helper/interface_definition/`.
- Add executor tests under `src/tests/executor/`. Run the full test set with
  `make test` from the repository root.
