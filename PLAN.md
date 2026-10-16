# Executor Implementation Plan

## File Format Design

### Page-based storage
- Page size: 4096 bytes (stored in header, configurable)
- Page 0: Header with directory of all schema elements
- Pages 1+: Data pages containing class/interface/function definitions and object data

### Header (Page 0) - v2
```
bytes 0-7:    magic "OOPDB\0\0\0" (unchanged)
bytes 8-11:   version u32 (= 2)
bytes 12-15:  page_size u32 (= 4096)
bytes 16-19:  total_pages u32
bytes 20-23:  free_list_head u32 (page index of first free page, 0 = none)
bytes 24-27:  class_count u32
bytes 28-31:  class_dir_page u32 (first page of class directory)
bytes 32-35:  interface_count u32
bytes 36-39:  interface_dir_page u32
bytes 40-43:  function_count u32
bytes 44-47:  function_dir_page u32
bytes 48-51:  object_count u32
bytes 52-55:  object_dir_page u32
bytes 56-59:  reserved
```
Total header: 60 bytes, fits within page 0.

### Directory Entries
Each class/interface/function has a directory entry:
- Name (length-prefixed string: u16 length + bytes)
- Location (page number + offset + length)
- For classes: parent name, implements list, fields list, object data page pointer
- For functions: argument types, return type, expression tree location

### Object Storage
- Each class has its own set of object pages
- Objects stored sequentially with ID + field values
- Free page list for reclaiming space on DELETE/DROP

## Implementation Phases

### Phase 1: Storage Foundation
- [ ] Update `src/interface/storage/header.h` - bump version to 2, add new header constants
- [ ] Update `src/interface/storage/database.h` - add page read/write/allocate/free functions, directory access
- [ ] Implement page I/O in `src/implementation/storage/database.c`
- [ ] Implement directory serialization for classes/interfaces/functions
- [ ] Implement object record serialization

### Phase 2: Executor Commands
- [ ] Schema commands (7 functions):
  - [ ] `src/implementation/executor/create_class.c` - CREATE CLASS
  - [ ] `src/implementation/executor/drop_class.c` - DROP CLASS
  - [ ] `src/implementation/executor/create_interface.c` - CREATE INTERFACE
  - [ ] `src/implementation/executor/drop_interface.c` - DROP INTERFACE
  - [ ] `src/implementation/executor/add_field.c` - ALTER CLASS ADD FIELD
  - [ ] `src/implementation/executor/drop_field.c` - ALTER CLASS DROP FIELD
  - [ ] `src/implementation/executor/rename_field.c` - ALTER CLASS RENAME FIELD
  - [ ] `src/implementation/executor/rename.c` - ALTER CLASS RENAME
  - [ ] `src/implementation/executor/extends.c` - ALTER CLASS EXTENDS
  - [ ] `src/implementation/executor/implements.c` - ALTER CLASS IMPLEMENTS
- [ ] Function commands (3 functions):
  - [ ] `src/implementation/executor/create_function.c` - CREATE FUNCTION
  - [ ] `src/implementation/executor/drop_function.c` - DROP FUNCTION
  - [ ] `src/implementation/executor/call.c` - CALL
- [ ] Object commands (4 functions):
  - [ ] `src/implementation/executor/new.c` - NEW
  - [ ] `src/implementation/executor/find.c` - FIND
  - [ ] `src/implementation/executor/set.c` - SET
  - [ ] `src/implementation/executor/delete.c` - DELETE
- [ ] Meta commands (3 functions):
  - [ ] `src/implementation/executor/list_classes.c` - LIST CLASSES
  - [ ] `src/implementation/executor/list_interfaces.c` - LIST INTERFACES
  - [ ] `src/implementation/executor/describe.c` - DESCRIBE

### Phase 3: Expression Evaluation
- [ ] Implement expression tree evaluator for CALL and FIND WHERE clauses
- [ ] Handle type coercion, pointer dereferencing, field access

### Phase 4: Tests & Polish
- [ ] Update `src/tests/executor/main.c` to verify actual behavior
- [ ] Test with README examples
- [ ] Error handling for constraint violations (duplicate names, invalid references, etc.)
- [ ] Run `make test` to verify all tests pass
- [ ] Run `make` to verify build