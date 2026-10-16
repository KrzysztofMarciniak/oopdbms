# Executor Implementation Plan

## File Format Design

### Page-based storage

-   Page size: 4096 bytes (stored in header, configurable; default 4096)
-   Page 0: Header with directory of all schema elements
-   Pages 1+: Data pages containing class/interface/function definitions
    and object data

### Header (Page 0) - v2 (little-endian)

    bytes 0-7:    magic "OOPDB\0\0\0" (unchanged)
    bytes 8-11:   version u32 (= 2)
    bytes 12-15:  page_size u32 (= 4096)
    bytes 16-19:  total_pages u32
    bytes 20-23:  free_list_head u32 (page index of first free page, 0 = none)
    bytes 24-27:  class_count u32
    bytes 28-31:  class_dir_page u32 (first page of class directory, 0 = none)
    bytes 32-35:  interface_count u32
    bytes 36-39:  interface_dir_page u32 (0 = none)
    bytes 40-43:  function_count u32
    bytes 44-47:  function_dir_page u32 (0 = none)
    bytes 48-51:  object_count u32 (global; may be unused if per-class)
    bytes 52-55:  object_dir_page u32 (reserved/0)
    bytes 56-59:  reserved

Total header: 60 bytes, fits within page 0. Use
`PAGE_INVALID = 0xFFFFFFFF` for "no page" where appropriate.

### Directory Format

Each directory is a linked chain of pages. Each directory page header: -
`u16 count` (number of entries on this page) - `u16 free_off` (offset to
free space, or 0 if packed; start simple with linear scan + append) -
`u32 next_dir_page` (0 if none)

Entries are variable-length. Common entry prefix: - `u16 name_len` -
`u32 loc_page` (location of definition blob) - `u16 loc_off` -
`u16 loc_len`

**Class directory entry** (after common prefix): - `u16 parent_len` (0
if none) - `u16 impl_count` (number of implemented interface names) -
`u32 obj_page` (first page of object data for this class; 0 if none) -
`u16 field_count` (optional snapshot) - data: `name bytes`,
`parent bytes` (if non-zero), then for each impl: `u16 len + bytes`

**Interface directory entry** (after common prefix): -
`u16 field_count` - data: `name bytes`

**Function directory entry** (after common prefix; key is class.func): -
`u16 class_len` - `u16 fname_len` - `u16 arg_count` - `u16 ret_len` -
data: `class bytes + fname bytes + ret bytes` (arg metadata may be
stored in definition blob)

Lookups are by name (and class for functions). Start with linear scan
across the directory page chain; index later if needed.

### Definition Blob Locations

`loc_page/loc_off/loc_len` point to serialized definition: - **Class
def**: `u16 field_count`; for each:
`u16 type_len + type + u16 name_len + name` - **Interface def**:
`u16 field_count`; for each:
`u16 type_len + type + u16 name_len + name` - **Function def**:
`u16 class_len + class + u16 fname_len + fname + u16 arg_count + (u16 type_len+type + u16 name_len+name for each arg) + u16 ret_len + ret + u32 expr_loc_page + u16 expr_loc_off + u16 expr_loc_len`

### Object Storage

-   Each class has its own object page chain starting at `obj_page` (in
    class dir). `obj_page == 0` means no objects yet.
-   Object page header:
    -   `u32 next_obj_page` (0 if none)
    -   `u32 free_off` (offset of free space from start of page,
        e.g. after header) or `u32 count`
-   Object record (variable length):
    -   `u32 obj_id` (unique per class in this design; assigned on NEW)
    -   `u32 flags` (e.g. 0=active, 1=deleted)
    -   `u32 data_len` (bytes of packed field values in class field
        order)
    -   `u8 data[]` (field values serialized by type)
-   Free page list (global): DB-level `free_list_head` points to first
    free page. Each free page stores `u32 next_free_page` at offset 0;
    rest zeroed. `db_alloc_page` pops from list or extends
    `total_pages`; `db_free_page` pushes to list.

## Implementation Phases

### Phase 1: Storage Foundation

-   [x] **1.1 Update header layout (v2)**
    -   [x] Update `src/interface/storage/header.h`: bump
        `STORAGE_VERSION` to `2ul`, add `DB_PAGE_SIZE`/`PAGE_SIZE`
        (4096), add header offset/constants for all v2 fields, set
        `STORAGE_HEADER_SIZE` to 60, add `PAGE_INVALID` (0xFFFFFFFF)
-   [x] **1.2 Define page + header types**
    -   [x] Create `src/interface/storage/page.h` (page size, header
        size, basic helpers)
    -   [x] Add `struct db_header` (little-endian view) in
        `src/interface/storage/database.h` (or new `header_layout.h`)
-   [x] **1.3 Extend database handle + API**
    -   [x] Update `src/interface/storage/database.h`: extend
        `struct database` with `FILE* fp`, `bool read_only`,
        `struct db_header hdr`, `uint8_t page_buf[4096]`
    -   [x] Add page I/O: `db_read_page`, `db_write_page`,
        `db_flush_header`
    -   [x] Add allocator: `db_alloc_page` (out_page_no), `db_free_page`
        (page_no)
    -   [x] Add directory access: `db_class_dir_find/put/remove`,
        `db_interface_dir_find/put/remove`,
        `db_function_dir_find/put/remove` (support class+name for
        functions)
-   [x] **1.4 Implement page I/O + header load/save**
    -   [x] Implement in `src/implementation/storage/database.c`:
        `database_init` (create/init v2 if new, else load+validate),
        `db_read_page`, `db_write_page`, `db_flush_header`,
        `database_save`
    -   [x] Header serialization/deserialization in little-endian
-   [x] **1.5 Page allocator + free list**
    -   [x] Create `src/implementation/storage/page_alloc.c` (or merge
        into database.c): pop/push free list, extend file, zero new
        pages, flush header on changes
-   [x] **1.6 Directory entry format + serialization**
    -   [x] Create `src/interface/storage/directory.h` with in-memory
        dir entry structs + API
    -   [x] Create `src/implementation/storage/directory.c`:
        serialize/deserialize entries, traverse linked dir pages,
        find/put/remove by name (linear scan; handle chain extension
        with `db_alloc_page`)
-   [x] **1.7 Object record format + serialization**
    -   [x] Create `src/interface/storage/record.h` with object record
        structs + helpers
    -   [x] Create `src/implementation/storage/record.c`: pack/unpack
        object records, object page chain traversal, append/find by id,
        mark deleted, allocate new obj_id per class (simple counter)
-   [x] **1.8 Wire into build**
    -   [x] Update `Makefile` to add new `.c` sources under storage
        (`page_alloc.c`, `directory.c`, `record.c` as needed)
-   [x] **1.9 Smoke test**
    -   [x] `make` builds cleanly with no warnings
    -   [x] Manual sanity: create empty DB, write/read page 0,
        alloc/free pages, basic dir put/find/remove

### Phase 2: Executor Commands

-   [ ] Schema commands (10 functions):
    -   [ ] `src/implementation/executor/create_class.c` - CREATE CLASS
    -   [ ] `src/implementation/executor/drop_class.c` - DROP CLASS
    -   [ ] `src/implementation/executor/create_interface.c` - CREATE
        INTERFACE
    -   [ ] `src/implementation/executor/drop_interface.c` - DROP
        INTERFACE
    -   [ ] `src/implementation/executor/add_field.c` - ALTER CLASS ADD
        FIELD
    -   [ ] `src/implementation/executor/drop_field.c` - ALTER CLASS
        DROP FIELD
    -   [ ] `src/implementation/executor/rename_field.c` - ALTER CLASS
        RENAME FIELD
    -   [ ] `src/implementation/executor/rename.c` - ALTER CLASS RENAME
    -   [ ] `src/implementation/executor/extends.c` - ALTER CLASS
        EXTENDS
    -   [ ] `src/implementation/executor/implements.c` - ALTER CLASS
        IMPLEMENTS
-   [ ] Function commands (3 functions):
    -   [ ] `src/implementation/executor/create_function.c` - CREATE
        FUNCTION
    -   [ ] `src/implementation/executor/drop_function.c` - DROP
        FUNCTION
    -   [ ] `src/implementation/executor/call.c` - CALL
-   [ ] Object commands (4 functions):
    -   [ ] `src/implementation/executor/new.c` - NEW
    -   [ ] `src/implementation/executor/find.c` - FIND
    -   [ ] `src/implementation/executor/set.c` - SET
    -   [ ] `src/implementation/executor/delete.c` - DELETE
-   [ ] Meta commands (3 functions):
    -   [ ] `src/implementation/executor/list_classes.c` - LIST CLASSES
    -   [ ] `src/implementation/executor/list_interfaces.c` - LIST
        INTERFACES
    -   [ ] `src/implementation/executor/describe.c` - DESCRIBE

### Phase 3: Expression Evaluation

-   [ ] Implement expression tree evaluator for CALL and FIND WHERE
    clauses
-   [ ] Handle type coercion, pointer dereferencing, field access
-   [ ] Support comparison operators, arithmetic as needed by
    expressions

### Phase 4: Tests & Polish

-   [ ] Update `src/tests/executor/main.c` to verify actual behavior
-   [ ] Test with README examples
-   [ ] Error handling for constraint violations (duplicate names,
    invalid references, missing entities, type mismatches, etc.)
-   [ ] Run `make test` to verify all tests pass
-   [ ] Run `make` to verify build is clean
