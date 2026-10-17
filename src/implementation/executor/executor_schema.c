/**
 * @file executor_schema.c
 * @brief Executor schema implementation.
 */

#include "implementation/executor/executor_internal.h"

static enum EXECUTOR_RESULT _create_interface(
    struct database* db, const struct analyzer_statement* statement) {
        struct executor_format_file file;
        struct executor_format_field* fields = NULL;
        struct executor_format_interface_entry entry;
        struct executor_format_interface_definition definition;
        uint8_t* definition_bytes = NULL;
        uint8_t entry_bytes[EXECUTOR_FORMAT_INTERFACE_ENTRY_SIZE];
        uint32_t max_id;
        uint32_t max_field_id;
        uint64_t definition_offset;
        uint64_t table_offset;
        size_t table_length;
        size_t definition_capacity;
        size_t definition_length;
        enum EXECUTOR_RESULT result = EXECUTOR_ERROR_BAD_STATEMENT;

        if (statement->name == NULL || statement->name[0] == '\0' ||
            strlen(statement->name) > UINT16_MAX ||
            statement->fields.count > UINT16_MAX ||
            (statement->fields.count != 0 && statement->fields.items == NULL) ||
            !db->format_mode || !executor_format_file_load(db, &file)) {
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }
        if (file.header.interface_count == UINT16_MAX) {
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }
        if (_find_interface(&file, statement->name, NULL)) {
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }
        if (!_max_interface_id(&file, &max_id) ||
            !_next_id(max_id, &entry.interface_id) ||
            !_max_field_id(&file, &max_field_id) ||
            !_collect_fields(&file, &statement->fields, &fields,
                             &max_field_id)) {
                return EXECUTOR_ERROR_STORAGE_FAILED;
        }
        definition.name        = (const uint8_t*)statement->name;
        definition.name_length = (uint16_t)strlen(statement->name);
        definition.fields      = fields;
        definition.field_count = (uint16_t)statement->fields.count;
        definition_capacity    = 4 + definition.name_length;
        {
                size_t i;
                for (i = 0; i < statement->fields.count; i++) {
                        size_t field_size;
                        if (!executor_helper_field_descriptor_size(
                                &fields[i], &field_size) ||
                            field_size > SIZE_MAX - definition_capacity) {
                                free(fields);
                                return EXECUTOR_ERROR_BAD_STATEMENT;
                        }
                        definition_capacity += field_size;
                }
        }
        definition_bytes = malloc(definition_capacity);
        if (definition_bytes == NULL ||
            !executor_helper_create_interface_definition(
                definition_bytes, definition_capacity, &definition,
                &definition_length) ||
            definition_length > UINT32_MAX ||
            file.size >
                UINT64_MAX - ((uint64_t)file.header.interface_count + 1) *
                                 EXECUTOR_FORMAT_INTERFACE_ENTRY_SIZE) {
                result = EXECUTOR_ERROR_STORAGE_FAILED;
                goto done;
        }
        table_length            = ((size_t)file.header.interface_count + 1) *
                                  EXECUTOR_FORMAT_INTERFACE_ENTRY_SIZE;
        entry.definition_offset = file.size + table_length;
        entry.definition_length = (uint32_t)definition_length;
        if (!executor_helper_create_interface_entry(
                entry_bytes, sizeof entry_bytes, &entry) ||
            !_append_new_table(&file, &file.metatable.interfaces,
                               file.header.interface_count,
                               EXECUTOR_FORMAT_INTERFACE_ENTRY_SIZE,
                               entry_bytes, &table_offset)) {
                result = EXECUTOR_ERROR_STORAGE_FAILED;
                goto done;
        }
        if (table_offset != file.size - table_length ||
            !executor_format_file_append(&file, definition_bytes,
                                         definition_length,
                                         &definition_offset) ||
            definition_offset != entry.definition_offset) {
                result = EXECUTOR_ERROR_STORAGE_FAILED;
                goto done;
        }
        file.metatable.interfaces.offset = table_offset;
        file.metatable.interfaces.length = table_length;
        file.header.interface_count++;
        result = executor_format_file_commit(&file)
                     ? EXECUTOR_OK
                     : EXECUTOR_ERROR_SAVE_FAILED;
done:
        free(definition_bytes);
        free(fields);
        return result;
}

static enum EXECUTOR_RESULT _create_class(
    struct database* db, const struct analyzer_statement* statement) {
        struct executor_format_file file;
        struct executor_format_field* fields = NULL;
        uint32_t* interface_ids              = NULL;
        struct executor_format_class_entry entry;
        struct executor_format_class_definition definition;
        uint8_t* definition_bytes = NULL;
        uint8_t entry_bytes[EXECUTOR_FORMAT_CLASS_ENTRY_SIZE];
        uint32_t max_id;
        uint32_t max_field_id;
        uint64_t definition_offset;
        uint64_t table_offset;
        size_t table_length;
        uint32_t parent_class_id = 0;
        size_t definition_capacity;
        size_t definition_length;
        size_t i;
        enum EXECUTOR_RESULT result = EXECUTOR_ERROR_BAD_STATEMENT;

        if (statement->name == NULL || statement->name[0] == '\0' ||
            strlen(statement->name) > UINT16_MAX ||
            statement->fields.count > UINT32_MAX ||
            statement->implements.count > UINT32_MAX ||
            (statement->fields.count != 0 && statement->fields.items == NULL) ||
            (statement->implements.count != 0 &&
             statement->implements.items == NULL) ||
            !db->format_mode || !executor_format_file_load(db, &file)) {
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }
        if (file.header.class_count == UINT16_MAX ||
            _find_class(&file, statement->name, NULL)) {
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }
        memset(&entry, 0, sizeof entry);
        if (!_max_class_id(&file, &max_id) ||
            !_next_id(max_id, &entry.class_id) ||
            !_max_field_id(&file, &max_field_id)) {
                return EXECUTOR_ERROR_STORAGE_FAILED;
        }
        if (statement->parent != NULL && statement->parent[0] != '\0') {
                struct executor_format_class_entry parent;
                size_t field_index;
                if (!_find_class(&file, statement->parent, &parent)) {
                        return EXECUTOR_ERROR_BAD_STATEMENT;
                }
                parent_class_id = parent.class_id;
                for (field_index = 0; field_index < statement->fields.count;
                     field_index++) {
                        if (_class_field_exists(
                                &file, parent_class_id,
                                statement->fields.items[field_index].name,
                                NULL)) {
                                return EXECUTOR_ERROR_BAD_STATEMENT;
                        }
                }
        }
        if (!_collect_fields(&file, &statement->fields, &fields,
                             &max_field_id)) {
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }
        if (statement->implements.count != 0) {
                interface_ids =
                    calloc(statement->implements.count, sizeof *interface_ids);
                if (interface_ids == NULL) {
                        free(fields);
                        return EXECUTOR_ERROR_STORAGE_FAILED;
                }
        }
        for (i = 0; i < statement->implements.count; i++) {
                struct executor_format_interface_entry interface_entry;
                size_t previous;
                const char* interface_name = statement->implements.items[i];

                if (interface_name == NULL ||
                    !_find_interface(&file, interface_name, &interface_entry)) {
                        free(fields);
                        free(interface_ids);
                        return EXECUTOR_ERROR_BAD_STATEMENT;
                }
                for (previous = 0; previous < i; previous++) {
                        if (interface_ids[previous] ==
                            interface_entry.interface_id) {
                                free(fields);
                                free(interface_ids);
                                return EXECUTOR_ERROR_BAD_STATEMENT;
                        }
                }
                if (!_interface_fields_satisfied(
                        &file, &interface_entry, fields,
                        statement->fields.count, parent_class_id)) {
                        free(fields);
                        free(interface_ids);
                        return EXECUTOR_ERROR_BAD_STATEMENT;
                }
                interface_ids[i] = interface_entry.interface_id;
        }

        definition.name            = (const uint8_t*)statement->name;
        definition.name_length     = (uint16_t)strlen(statement->name);
        definition.flags           = 0;
        definition.parent_class_id = parent_class_id;
        definition.fields          = fields;
        definition.field_count     = (uint32_t)statement->fields.count;
        definition.interface_ids   = interface_ids;
        definition.interface_count = (uint32_t)statement->implements.count;
        definition_capacity        = 16 + definition.name_length;
        for (i = 0; i < statement->fields.count; i++) {
                size_t field_size;
                if (!executor_helper_field_descriptor_size(&fields[i],
                                                           &field_size) ||
                    field_size > SIZE_MAX - definition_capacity) {
                        free(fields);
                        free(interface_ids);
                        return EXECUTOR_ERROR_BAD_STATEMENT;
                }
                definition_capacity += field_size;
        }
        if (statement->implements.count >
            (SIZE_MAX - definition_capacity) / sizeof(uint32_t)) {
                free(fields);
                free(interface_ids);
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }
        definition_capacity += statement->implements.count * sizeof(uint32_t);
        definition_bytes = malloc(definition_capacity);
        if (definition_bytes == NULL ||
            !executor_helper_create_class_definition(
                definition_bytes, definition_capacity, &definition,
                &definition_length) ||
            definition_length > UINT32_MAX ||
            file.size > UINT64_MAX - ((uint64_t)file.header.class_count + 1) *
                                         EXECUTOR_FORMAT_CLASS_ENTRY_SIZE) {
                result = EXECUTOR_ERROR_STORAGE_FAILED;
                goto done;
        }
        table_length            = ((size_t)file.header.class_count + 1) *
                                  EXECUTOR_FORMAT_CLASS_ENTRY_SIZE;
        entry.definition_offset = file.size + table_length;
        entry.definition_length = (uint32_t)definition_length;
        entry.next_object_id    = 1;
        if (!executor_helper_create_class_entry(entry_bytes, sizeof entry_bytes,
                                                &entry) ||
            !_append_new_table(
                &file, &file.metatable.classes, file.header.class_count,
                EXECUTOR_FORMAT_CLASS_ENTRY_SIZE, entry_bytes, &table_offset)) {
                result = EXECUTOR_ERROR_STORAGE_FAILED;
                goto done;
        }
        if (table_offset != file.size - table_length ||
            !executor_format_file_append(&file, definition_bytes,
                                         definition_length,
                                         &definition_offset) ||
            definition_offset != entry.definition_offset) {
                result = EXECUTOR_ERROR_STORAGE_FAILED;
                goto done;
        }
        file.metatable.classes.offset = table_offset;
        file.metatable.classes.length = table_length;
        file.header.class_count++;
        result = executor_format_file_commit(&file)
                     ? EXECUTOR_OK
                     : EXECUTOR_ERROR_SAVE_FAILED;
done:
        free(definition_bytes);
        free(fields);
        free(interface_ids);
        return result;
}

static enum EXECUTOR_RESULT _alter_class(
    struct database* db, const struct analyzer_statement* statement) {
        struct executor_format_file file;
        struct executor_format_class_entry entry;
        struct executor_class_view view;
        size_t class_index;
        uint8_t* renamed = NULL;
        size_t i;
        enum EXECUTOR_RESULT result = EXECUTOR_ERROR_BAD_STATEMENT;

        if (statement->name == NULL || !db->format_mode ||
            !executor_format_file_load(db, &file) ||
            !_class_index_by_name(&file, statement->name, &class_index,
                                  &entry) ||
            !_load_class_view(&file, &entry, &view)) {
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }
        switch (statement->kind) {
                case ANALYZER_ALTER_CLASS_ADD_FIELD: {
                        struct executor_format_field* expanded;
                        uint32_t max_field_id;
                        struct analyzer_fields one          = statement->fields;
                        struct executor_format_field* field = NULL;

                        if (_class_or_descendant_has_objects(&file,
                                                             entry.class_id) ||
                            statement->fields.count != 1 ||
                            statement->fields.items == NULL ||
                            view.field_count == UINT32_MAX ||
                            _class_field_exists(&file, entry.class_id,
                                                statement->fields.items[0].name,
                                                NULL) ||
                            _descendant_defines_field(
                                &file, entry.class_id,
                                statement->fields.items[0].name) ||
                            !_max_field_id(&file, &max_field_id) ||
                            !_collect_fields(&file, &one, &field,
                                             &max_field_id)) {
                                free(field);
                                break;
                        }
                        expanded =
                            realloc(view.fields, (view.field_count + 1) *
                                                     sizeof *view.fields);
                        if (expanded == NULL) {
                                free(field);
                                result = EXECUTOR_ERROR_STORAGE_FAILED;
                                break;
                        }
                        view.fields                     = expanded;
                        view.fields[view.field_count++] = field[0];
                        free(field);
                        result =
                            _store_class_view(&file, class_index, &entry, &view)
                                ? EXECUTOR_OK
                                : EXECUTOR_ERROR_STORAGE_FAILED;
                        break;
                }
                case ANALYZER_ALTER_CLASS_DROP_FIELD: {
                        size_t field_index;
                        if (_class_or_descendant_has_objects(&file,
                                                             entry.class_id) ||
                            statement->field == NULL) {
                                break;
                        }
                        for (field_index = 0; field_index < view.field_count;
                             field_index++) {
                                if (_field_name_equal(&view.fields[field_index],
                                                      statement->field)) {
                                        break;
                                }
                        }
                        if (field_index == view.field_count) {
                                break;
                        }
                        memmove(&view.fields[field_index],
                                &view.fields[field_index + 1],
                                (view.field_count - field_index - 1) *
                                    sizeof *view.fields);
                        view.field_count--;
                        result =
                            _store_class_view(&file, class_index, &entry, &view)
                                ? EXECUTOR_OK
                                : EXECUTOR_ERROR_STORAGE_FAILED;
                        break;
                }
                case ANALYZER_ALTER_CLASS_RENAME_FIELD: {
                        size_t field_index;
                        if (statement->field == NULL ||
                            statement->new_name == NULL ||
                            statement->new_name[0] == '\0' ||
                            strlen(statement->new_name) > UINT16_MAX ||
                            _class_or_descendant_has_objects(&file,
                                                             entry.class_id) ||
                            _class_field_exists(&file, entry.class_id,
                                                statement->new_name, NULL) ||
                            _descendant_defines_field(&file, entry.class_id,
                                                      statement->new_name)) {
                                break;
                        }
                        for (field_index = 0; field_index < view.field_count;
                             field_index++) {
                                if (_field_name_equal(&view.fields[field_index],
                                                      statement->field)) {
                                        break;
                                }
                        }
                        if (field_index == view.field_count) {
                                break;
                        }
                        renamed = (uint8_t*)malloc(strlen(statement->new_name));
                        if (renamed == NULL) {
                                result = EXECUTOR_ERROR_STORAGE_FAILED;
                                break;
                        }
                        memcpy(renamed, statement->new_name,
                               strlen(statement->new_name));
                        view.fields[field_index].name = renamed;
                        view.fields[field_index].name_length =
                            (uint16_t)strlen(statement->new_name);
                        result =
                            _store_class_view(&file, class_index, &entry, &view)
                                ? EXECUTOR_OK
                                : EXECUTOR_ERROR_STORAGE_FAILED;
                        break;
                }
                case ANALYZER_ALTER_CLASS_RENAME: {
                        if (statement->new_name == NULL ||
                            statement->new_name[0] == '\0' ||
                            strlen(statement->new_name) > UINT16_MAX ||
                            _find_class(&file, statement->new_name, NULL)) {
                                break;
                        }
                        renamed = (uint8_t*)malloc(strlen(statement->new_name));
                        if (renamed == NULL) {
                                result = EXECUTOR_ERROR_STORAGE_FAILED;
                                break;
                        }
                        memcpy(renamed, statement->new_name,
                               strlen(statement->new_name));
                        view.name = renamed;
                        view.name_length =
                            (uint16_t)strlen(statement->new_name);
                        result =
                            _store_class_view(&file, class_index, &entry, &view)
                                ? EXECUTOR_OK
                                : EXECUTOR_ERROR_STORAGE_FAILED;
                        break;
                }
                case ANALYZER_ALTER_CLASS_EXTENDS: {
                        struct executor_format_class_entry parent;
                        uint32_t parent_id = 0;
                        uint32_t cursor;
                        size_t walked        = 0;
                        bool cycle           = false;
                        bool duplicate_field = false;

                        if (_class_or_descendant_has_objects(&file,
                                                             entry.class_id) ||
                            statement->parent == NULL ||
                            !_find_class(&file, statement->parent, &parent) ||
                            parent.class_id == entry.class_id) {
                                break;
                        }
                        cursor = parent.class_id;
                        while (cursor != 0 &&
                               walked++ <= file.header.class_count) {
                                struct executor_format_class_entry ancestor;
                                struct executor_class_view ancestor_view;
                                if (cursor == entry.class_id) {
                                        cycle = true;
                                        break;
                                }
                                if (!_class_entry_by_id(&file, cursor,
                                                        &ancestor) ||
                                    !_load_class_view(&file, &ancestor,
                                                      &ancestor_view)) {
                                        cycle = true;
                                        break;
                                }
                                cursor = ancestor_view.parent_class_id;
                                _class_view_free(&ancestor_view);
                        }
                        if (cycle ||
                            walked > (size_t)file.header.class_count + 1) {
                                break;
                        }
                        parent_id = parent.class_id;
                        cursor    = parent_id;
                        while (cursor != 0) {
                                struct executor_format_class_entry ancestor;
                                struct executor_class_view ancestor_view;
                                if (!_class_entry_by_id(&file, cursor,
                                                        &ancestor) ||
                                    !_load_class_view(&file, &ancestor,
                                                      &ancestor_view)) {
                                        cursor = 0;
                                        break;
                                }
                                for (i = 0; i < view.field_count; i++) {
                                        size_t j;
                                        for (j = 0;
                                             j < ancestor_view.field_count;
                                             j++) {
                                                if (view.fields[i]
                                                            .name_length ==
                                                        ancestor_view.fields[j]
                                                            .name_length &&
                                                    memcmp(
                                                        view.fields[i].name,
                                                        ancestor_view.fields[j]
                                                            .name,
                                                        view.fields[i]
                                                            .name_length) ==
                                                        0) {
                                                        duplicate_field = true;
                                                        break;
                                                }
                                        }
                                        if (duplicate_field) {
                                                break;
                                        }
                                }
                                if (duplicate_field) {
                                        _class_view_free(&ancestor_view);
                                        break;
                                }
                                cursor = ancestor_view.parent_class_id;
                                _class_view_free(&ancestor_view);
                        }
                        if (duplicate_field) {
                                break;
                        }
                        view.parent_class_id = parent_id;
                        result =
                            _store_class_view(&file, class_index, &entry, &view)
                                ? EXECUTOR_OK
                                : EXECUTOR_ERROR_STORAGE_FAILED;
                        break;
                }
                case ANALYZER_ALTER_CLASS_IMPLEMENTS: {
                        struct executor_format_interface_entry interface_entry;
                        struct executor_interface_view interface_view;
                        uint32_t* expanded;
                        bool valid = true;

                        if (statement->implements.count != 1 ||
                            statement->implements.items == NULL ||
                            !_find_interface(&file,
                                             statement->implements.items[0],
                                             &interface_entry) ||
                            !_load_interface_view(&file, &interface_entry,
                                                  &interface_view)) {
                                break;
                        }
                        for (i = 0; i < view.interface_count; i++) {
                                if (view.interface_ids[i] ==
                                    interface_entry.interface_id) {
                                        valid = false;
                                }
                        }
                        for (i = 0; valid && i < interface_view.field_count;
                             i++) {
                                struct executor_format_field actual;
                                valid =
                                    _class_field_by_bytes(
                                        &file, entry.class_id,
                                        interface_view.fields[i].name,
                                        interface_view.fields[i].name_length,
                                        &actual) &&
                                    _field_types_equal(
                                        &actual, &interface_view.fields[i]);
                        }
                        if (valid && view.interface_count != UINT32_MAX) {
                                expanded =
                                    realloc(view.interface_ids,
                                            (view.interface_count + 1) *
                                                sizeof *view.interface_ids);
                                if (expanded == NULL) {
                                        result = EXECUTOR_ERROR_STORAGE_FAILED;
                                } else {
                                        view.interface_ids = expanded;
                                        view.interface_ids
                                            [view.interface_count++] =
                                            interface_entry.interface_id;
                                        result =
                                            _store_class_view(&file,
                                                              class_index,
                                                              &entry, &view)
                                                ? EXECUTOR_OK
                                                : EXECUTOR_ERROR_STORAGE_FAILED;
                                }
                        }
                        _interface_view_free(&interface_view);
                        break;
                }
                default:
                        break;
        }
        if (result == EXECUTOR_OK && !executor_format_file_commit(&file)) {
                result = EXECUTOR_ERROR_SAVE_FAILED;
        }
        free(renamed);
        _class_view_free(&view);
        return result;
}

static enum EXECUTOR_RESULT _drop_class(
    struct database* db, const struct analyzer_statement* statement) {
        struct executor_format_file file;
        struct executor_format_class_entry target;
        size_t index;

        if (statement->name == NULL || !db->format_mode ||
            !executor_format_file_load(db, &file) ||
            !_class_index_by_name(&file, statement->name, &index, &target)) {
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }
        if (target.object_count != 0 || target.data_length != 0 ||
            _class_has_external_references(&file, &target) ||
            !_remove_class_table_entry(&file, index)) {
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }
        return executor_format_file_commit(&file) ? EXECUTOR_OK
                                                  : EXECUTOR_ERROR_SAVE_FAILED;
}

static enum EXECUTOR_RESULT _drop_interface(
    struct database* db, const struct analyzer_statement* statement) {
        struct executor_format_file file;
        struct executor_format_interface_entry target;
        size_t index;
        size_t i;

        if (statement->name == NULL || !db->format_mode ||
            !executor_format_file_load(db, &file) ||
            !_interface_index_by_name(&file, statement->name, &index,
                                      &target)) {
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }
        for (i = 0; i < file.header.class_count; i++) {
                struct executor_format_class_entry class_entry;
                struct executor_class_view view;
                size_t j;
                if (!executor_format_file_read_class_entry(&file, i,
                                                           &class_entry) ||
                    !_load_class_view(&file, &class_entry, &view)) {
                        return EXECUTOR_ERROR_STORAGE_FAILED;
                }
                for (j = 0; j < view.interface_count; j++) {
                        if (view.interface_ids[j] == target.interface_id) {
                                _class_view_free(&view);
                                return EXECUTOR_ERROR_BAD_STATEMENT;
                        }
                }
                _class_view_free(&view);
        }
        if (!_remove_interface_table_entry(&file, index)) {
                return EXECUTOR_ERROR_STORAGE_FAILED;
        }
        return executor_format_file_commit(&file) ? EXECUTOR_OK
                                                  : EXECUTOR_ERROR_SAVE_FAILED;
}

enum EXECUTOR_RESULT executor_schema_execute(
    struct database* db, const struct analyzer_statement* statement) {
        switch (statement->kind) {
                case ANALYZER_CREATE_CLASS:
                        return _create_class(db, statement);
                case ANALYZER_CREATE_INTERFACE:
                        return _create_interface(db, statement);
                case ANALYZER_DROP_CLASS:
                        return _drop_class(db, statement);
                case ANALYZER_DROP_INTERFACE:
                        return _drop_interface(db, statement);
                case ANALYZER_ALTER_CLASS_ADD_FIELD:
                case ANALYZER_ALTER_CLASS_DROP_FIELD:
                case ANALYZER_ALTER_CLASS_RENAME_FIELD:
                case ANALYZER_ALTER_CLASS_RENAME:
                case ANALYZER_ALTER_CLASS_EXTENDS:
                case ANALYZER_ALTER_CLASS_IMPLEMENTS:
                        return _alter_class(db, statement);
                default:
                        return EXECUTOR_ERROR_NOT_IMPLEMENTED;
        }
}
