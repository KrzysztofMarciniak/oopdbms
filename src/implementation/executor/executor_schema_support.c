/**
 * @file executor_schema_support.c
 * @brief Executor schema support implementation.
 */

#include "implementation/executor/executor_internal.h"

void _class_view_free(struct executor_class_view* view) {
        free(view->fields);
        free(view->interface_ids);
        free(view->bytes);
        memset(view, 0, sizeof *view);
}

void _interface_view_free(struct executor_interface_view* view) {
        free(view->fields);
        free(view->bytes);
        memset(view, 0, sizeof *view);
}

bool _load_class_view(struct executor_format_file* file,
                      const struct executor_format_class_entry* entry,
                      struct executor_class_view* view) {
        uint64_t fields_allocation_size;
        size_t offset;
        size_t i;

        memset(view, 0, sizeof *view);
        if (!_read_definition(file, entry->definition_offset,
                              entry->definition_length, &view->bytes) ||
            entry->definition_length < 16) {
                _class_view_free(view);
                return false;
        }
        view->name_length     = executor_helper_read_u16(view->bytes);
        view->flags           = executor_helper_read_u16(view->bytes + 2);
        view->parent_class_id = executor_helper_read_u32(view->bytes + 4);
        view->field_count     = executor_helper_read_u32(view->bytes + 8);
        view->interface_count = executor_helper_read_u32(view->bytes + 12);
        view->name            = view->bytes + 16;
        offset                = 16;
        if (view->name_length > entry->definition_length - offset) {
                _class_view_free(view);
                return false;
        }
        offset += view->name_length;
        if (!executor_helper_multiply_u64(view->field_count,
                                          sizeof *view->fields,
                                          &fields_allocation_size) ||
            fields_allocation_size > (uint64_t)SIZE_MAX) {
                _class_view_free(view);
                return false;
        }
        view->fields = calloc(view->field_count == 0 ? 1 : view->field_count,
                              sizeof *view->fields);
        if (view->fields == NULL) {
                _class_view_free(view);
                return false;
        }
        for (i = 0; i < view->field_count; i++) {
                if (!executor_helper_read_field_descriptor(
                        view->bytes, entry->definition_length, &offset,
                        &view->fields[i])) {
                        _class_view_free(view);
                        return false;
                }
        }
        if ((uint64_t)view->interface_count * 4 >
            entry->definition_length - offset) {
                _class_view_free(view);
                return false;
        }
        if (view->interface_count != 0) {
                view->interface_ids =
                    calloc(view->interface_count, sizeof *view->interface_ids);
                if (view->interface_ids == NULL) {
                        _class_view_free(view);
                        return false;
                }
                for (i = 0; i < view->interface_count; i++) {
                        view->interface_ids[i] =
                            executor_helper_read_u32(view->bytes + offset);
                        offset += 4;
                }
        }
        return offset == entry->definition_length;
}

bool _load_interface_view(struct executor_format_file* file,
                          const struct executor_format_interface_entry* entry,
                          struct executor_interface_view* view) {
        size_t offset;
        size_t i;

        memset(view, 0, sizeof *view);
        if (!_read_definition(file, entry->definition_offset,
                              entry->definition_length, &view->bytes) ||
            entry->definition_length < 4) {
                _interface_view_free(view);
                return false;
        }
        view->name_length = executor_helper_read_u16(view->bytes);
        view->field_count = executor_helper_read_u16(view->bytes + 2);
        offset            = 4;
        if (view->name_length > entry->definition_length - offset) {
                _interface_view_free(view);
                return false;
        }
        offset += view->name_length;
        view->fields = calloc(view->field_count == 0 ? 1 : view->field_count,
                              sizeof *view->fields);
        if (view->fields == NULL) {
                _interface_view_free(view);
                return false;
        }
        for (i = 0; i < view->field_count; i++) {
                if (!executor_helper_read_field_descriptor(
                        view->bytes, entry->definition_length, &offset,
                        &view->fields[i])) {
                        _interface_view_free(view);
                        return false;
                }
        }
        return offset == entry->definition_length;
}

bool _field_name_equal(const struct executor_format_field* field,
                       const char* name) {
        return field != NULL && name != NULL &&
               field->name_length == strlen(name) &&
               memcmp(field->name, name, field->name_length) == 0;
}

bool _field_types_equal(const struct executor_format_field* left,
                        const struct executor_format_field* right) {
        return left->type_code == right->type_code &&
               (left->type_code != EXECUTOR_FORMAT_TYPE_POINTER ||
                left->target_class_id == right->target_class_id);
}

bool _class_field_exists(struct executor_format_file* file, uint32_t class_id,
                         const char* name,
                         struct executor_format_field* found) {
        size_t visited = 0;

        while (class_id != 0 && visited++ <= file->header.class_count) {
                struct executor_format_class_entry entry;
                struct executor_class_view view;
                size_t i;
                bool has_parent;

                if (!_class_entry_by_id(file, class_id, &entry) ||
                    !_load_class_view(file, &entry, &view)) {
                        return false;
                }
                has_parent = view.parent_class_id != 0;
                for (i = 0; i < view.field_count; i++) {
                        if (_field_name_equal(&view.fields[i], name)) {
                                if (found != NULL) {
                                        *found = view.fields[i];
                                }
                                class_id = 0;
                                _class_view_free(&view);
                                return true;
                        }
                }
                class_id = view.parent_class_id;
                _class_view_free(&view);
                if (!has_parent) {
                        break;
                }
        }
        return false;
}

bool _class_field_by_bytes(struct executor_format_file* file, uint32_t class_id,
                           const uint8_t* name, uint16_t name_length,
                           struct executor_format_field* found) {
        size_t visited = 0;

        while (class_id != 0 && visited++ <= file->header.class_count) {
                struct executor_format_class_entry entry;
                struct executor_class_view view;
                size_t i;
                uint32_t parent_id;

                if (!_class_entry_by_id(file, class_id, &entry) ||
                    !_load_class_view(file, &entry, &view)) {
                        return false;
                }
                parent_id = view.parent_class_id;
                for (i = 0; i < view.field_count; i++) {
                        if (view.fields[i].name_length == name_length &&
                            memcmp(view.fields[i].name, name, name_length) ==
                                0) {
                                if (found != NULL) {
                                        *found = view.fields[i];
                                }
                                _class_view_free(&view);
                                return true;
                        }
                }
                _class_view_free(&view);
                class_id = parent_id;
        }
        return false;
}

bool _interface_fields_satisfied(
    struct executor_format_file* file,
    const struct executor_format_interface_entry* entry,
    const struct executor_format_field* fields, size_t field_count,
    uint32_t parent_class_id) {
        struct executor_interface_view view;
        size_t i;
        bool valid = true;

        if (!_load_interface_view(file, entry, &view)) {
                return false;
        }
        for (i = 0; i < view.field_count && valid; i++) {
                struct executor_format_field actual;
                size_t j;
                bool found = false;

                for (j = 0; j < field_count; j++) {
                        if (fields[j].name_length ==
                                view.fields[i].name_length &&
                            memcmp(fields[j].name, view.fields[i].name,
                                   fields[j].name_length) == 0) {
                                actual = fields[j];
                                found  = true;
                                break;
                        }
                }
                if (!found && parent_class_id != 0) {
                        found = _class_field_by_bytes(
                            file, parent_class_id, view.fields[i].name,
                            view.fields[i].name_length, &actual);
                }
                valid = found && _field_types_equal(&actual, &view.fields[i]);
        }
        _interface_view_free(&view);
        return valid;
}

bool _class_or_descendant_has_objects(struct executor_format_file* file,
                                      uint32_t class_id) {
        size_t i;

        for (i = 0; i < file->header.class_count; i++) {
                struct executor_format_class_entry candidate;
                uint32_t cursor;
                size_t depth = 0;

                if (!executor_format_file_read_class_entry(file, i,
                                                           &candidate)) {
                        return true;
                }
                cursor = candidate.class_id;
                while (cursor != 0 && depth++ <= file->header.class_count) {
                        struct executor_format_class_entry current;
                        struct executor_class_view view;
                        uint32_t parent_id;

                        if (cursor == class_id &&
                            (candidate.object_count != 0 ||
                             candidate.data_length != 0)) {
                                return true;
                        }
                        if (!_class_entry_by_id(file, cursor, &current) ||
                            !_load_class_view(file, &current, &view)) {
                                return true;
                        }
                        parent_id = view.parent_class_id;
                        _class_view_free(&view);
                        cursor = parent_id;
                }
                if (cursor != 0) {
                        return true;
                }
        }
        return false;
}

bool _descendant_defines_field(struct executor_format_file* file,
                               uint32_t class_id, const char* name) {
        size_t i;

        for (i = 0; i < file->header.class_count; i++) {
                struct executor_format_class_entry candidate;
                uint32_t cursor;
                size_t depth       = 0;
                bool is_descendant = false;

                if (!executor_format_file_read_class_entry(file, i,
                                                           &candidate)) {
                        return true;
                }
                if (candidate.class_id == class_id) {
                        continue;
                }
                cursor = candidate.class_id;
                while (cursor != 0 && depth++ <= file->header.class_count) {
                        struct executor_format_class_entry current;
                        struct executor_class_view view;
                        uint32_t parent_id;

                        if (!_class_entry_by_id(file, cursor, &current) ||
                            !_load_class_view(file, &current, &view)) {
                                return true;
                        }
                        parent_id = view.parent_class_id;
                        _class_view_free(&view);
                        if (parent_id == class_id) {
                                is_descendant = true;
                                break;
                        }
                        cursor = parent_id;
                }
                if (cursor != 0 &&
                    depth > (size_t)file->header.class_count + 1) {
                        return true;
                }
                if (is_descendant) {
                        struct executor_class_view view;
                        size_t field_index;

                        if (!_load_class_view(file, &candidate, &view)) {
                                return true;
                        }
                        for (field_index = 0; field_index < view.field_count;
                             field_index++) {
                                if (_field_name_equal(&view.fields[field_index],
                                                      name)) {
                                        _class_view_free(&view);
                                        return true;
                                }
                        }
                        _class_view_free(&view);
                }
        }
        return false;
}

void _free_schema_fields(struct executor_format_field* fields, size_t count) {
        size_t i;
        for (i = 0; i < count; i++) {
                free((void*)fields[i].name);
        }
        free(fields);
}

bool _collect_schema_fields(struct executor_format_file* file,
                            uint32_t class_id,
                            struct executor_format_field** output,
                            size_t* output_count) {
        uint32_t* lineage;
        size_t lineage_count = 0;
        size_t i;
        struct executor_format_field* fields = NULL;
        size_t count                         = 0;

        lineage = calloc((size_t)file->header.class_count + 1, sizeof *lineage);
        if (lineage == NULL) {
                return false;
        }
        while (class_id != 0 && lineage_count <= file->header.class_count) {
                struct executor_format_class_entry entry;
                struct executor_class_view view;
                if (!_class_entry_by_id(file, class_id, &entry) ||
                    !_load_class_view(file, &entry, &view)) {
                        free(lineage);
                        return false;
                }
                lineage[lineage_count++] = class_id;
                class_id                 = view.parent_class_id;
                _class_view_free(&view);
        }
        if (class_id != 0) {
                free(lineage);
                return false;
        }
        for (i = lineage_count; i > 0; i--) {
                struct executor_format_class_entry entry;
                struct executor_class_view view;
                size_t j;

                if (!_class_entry_by_id(file, lineage[i - 1], &entry) ||
                    !_load_class_view(file, &entry, &view)) {
                        _free_schema_fields(fields, count);
                        free(lineage);
                        return false;
                }
                for (j = 0; j < view.field_count; j++) {
                        struct executor_format_field* expanded;
                        uint8_t* name;
                        size_t k;

                        for (k = 0; k < count; k++) {
                                if (fields[k].name_length ==
                                        view.fields[j].name_length &&
                                    memcmp(fields[k].name, view.fields[j].name,
                                           fields[k].name_length) == 0) {
                                        _class_view_free(&view);
                                        _free_schema_fields(fields, count);
                                        free(lineage);
                                        return false;
                                }
                        }
                        name = malloc((size_t)view.fields[j].name_length + 1);
                        if (name == NULL) {
                                _class_view_free(&view);
                                _free_schema_fields(fields, count);
                                free(lineage);
                                return false;
                        }
                        memcpy(name, view.fields[j].name,
                               view.fields[j].name_length);
                        name[view.fields[j].name_length] = '\0';
                        expanded =
                            realloc(fields, (count + 1) * sizeof *fields);
                        if (expanded == NULL) {
                                free(name);
                                _class_view_free(&view);
                                _free_schema_fields(fields, count);
                                free(lineage);
                                return false;
                        }
                        fields             = expanded;
                        fields[count]      = view.fields[j];
                        fields[count].name = name;
                        count++;
                }
                _class_view_free(&view);
        }
        free(lineage);
        *output       = fields;
        *output_count = count;
        return true;
}

bool _class_entry_by_id(struct executor_format_file* file, uint32_t class_id,
                        struct executor_format_class_entry* entry) {
        size_t i;

        for (i = 0; i < file->header.class_count; i++) {
                if (!executor_format_file_read_class_entry(file, i, entry)) {
                        return false;
                }
                if (entry->class_id == class_id) {
                        return true;
                }
        }
        return false;
}

bool _interface_entry_by_id(struct executor_format_file* file,
                            uint32_t interface_id,
                            struct executor_format_interface_entry* entry) {
        size_t i;

        for (i = 0; i < file->header.interface_count; i++) {
                if (!executor_format_file_read_interface_entry(file, i,
                                                               entry)) {
                        return false;
                }
                if (entry->interface_id == interface_id) {
                        return true;
                }
        }
        return false;
}

bool _rewrite_class_entry(struct executor_format_file* file, size_t index,
                          const struct executor_format_class_entry* entry,
                          const uint8_t* definition, size_t definition_length) {
        size_t count = file->header.class_count;
        size_t length;
        uint8_t* table;
        uint8_t entry_bytes[EXECUTOR_FORMAT_CLASS_ENTRY_SIZE];
        uint64_t definition_offset;
        uint64_t table_offset;
        bool result = false;

        if (index >= count || count > SIZE_MAX / sizeof entry_bytes ||
            definition_length > UINT32_MAX ||
            !executor_format_file_append(file, definition, definition_length,
                                         &definition_offset)) {
                return false;
        }
        {
                struct executor_format_class_entry updated = *entry;
                updated.definition_offset                  = definition_offset;
                updated.definition_length = (uint32_t)definition_length;
                if (!executor_helper_create_class_entry(
                        entry_bytes, sizeof entry_bytes, &updated)) {
                        return false;
                }
        }
        length = count * sizeof entry_bytes;
        table  = malloc(length);
        if (table == NULL ||
            !executor_format_file_read(file, file->metatable.classes.offset,
                                       table, length)) {
                free(table);
                return false;
        }
        memcpy(table + index * sizeof entry_bytes, entry_bytes,
               sizeof entry_bytes);
        result = _append_table_bytes(file, table, sizeof entry_bytes, count,
                                     &table_offset);
        free(table);
        if (!result) {
                return false;
        }
        file->metatable.classes.offset = table_offset;
        file->metatable.classes.length = length;
        return true;
}

static bool _build_class_definition(const struct executor_class_view* view,
                                    uint8_t** output, size_t* output_length) {
        struct executor_format_class_definition definition;
        size_t capacity = 16 + view->name_length;
        uint8_t* bytes;
        size_t i;

        for (i = 0; i < view->field_count; i++) {
                size_t field_size;
                if (!executor_helper_field_descriptor_size(&view->fields[i],
                                                           &field_size) ||
                    field_size > SIZE_MAX - capacity) {
                        return false;
                }
                capacity += field_size;
        }
        if (view->interface_count > (SIZE_MAX - capacity) / 4) {
                return false;
        }
        capacity += view->interface_count * 4;
        bytes = malloc(capacity);
        if (bytes == NULL) {
                return false;
        }
        definition.name            = view->name;
        definition.name_length     = view->name_length;
        definition.flags           = view->flags;
        definition.parent_class_id = view->parent_class_id;
        definition.fields          = view->fields;
        definition.field_count     = view->field_count;
        definition.interface_ids   = view->interface_ids;
        definition.interface_count = view->interface_count;
        if (!executor_helper_create_class_definition(
                bytes, capacity, &definition, output_length)) {
                free(bytes);
                return false;
        }
        *output = bytes;
        return true;
}

bool _class_index_by_name(struct executor_format_file* file, const char* name,
                          size_t* index,
                          struct executor_format_class_entry* entry) {
        size_t i;

        for (i = 0; i < file->header.class_count; i++) {
                struct executor_format_class_entry current;
                uint8_t* definition;
                bool matches;

                if (!executor_format_file_read_class_entry(file, i, &current) ||
                    !_read_definition(file, current.definition_offset,
                                      current.definition_length, &definition)) {
                        return false;
                }
                matches = _name_is(definition, current.definition_length, 0, 16,
                                   name);
                free(definition);
                if (matches) {
                        if (index != NULL) {
                                *index = i;
                        }
                        if (entry != NULL) {
                                *entry = current;
                        }
                        return true;
                }
        }
        return false;
}

bool _interface_index_by_name(struct executor_format_file* file,
                              const char* name, size_t* index,
                              struct executor_format_interface_entry* entry) {
        size_t i;

        for (i = 0; i < file->header.interface_count; i++) {
                struct executor_format_interface_entry current;
                uint8_t* definition;
                bool matches;

                if (!executor_format_file_read_interface_entry(file, i,
                                                               &current) ||
                    !_read_definition(file, current.definition_offset,
                                      current.definition_length, &definition)) {
                        return false;
                }
                matches =
                    _name_is(definition, current.definition_length, 0, 4, name);
                free(definition);
                if (matches) {
                        if (index != NULL) {
                                *index = i;
                        }
                        if (entry != NULL) {
                                *entry = current;
                        }
                        return true;
                }
        }
        return false;
}

bool _store_class_view(struct executor_format_file* file, size_t index,
                       const struct executor_format_class_entry* entry,
                       const struct executor_class_view* view) {
        uint8_t* definition = NULL;
        size_t definition_length;
        bool result;

        if (!_build_class_definition(view, &definition, &definition_length)) {
                return false;
        }
        result = _rewrite_class_entry(file, index, entry, definition,
                                      definition_length);
        free(definition);
        return result;
}

bool _remove_class_table_entry(struct executor_format_file* file,
                               size_t remove_index) {
        size_t old_count  = file->header.class_count;
        size_t new_count  = old_count - 1;
        size_t entry_size = EXECUTOR_FORMAT_CLASS_ENTRY_SIZE;
        uint8_t* table    = NULL;
        uint64_t table_offset;
        size_t i;

        if (remove_index >= old_count || old_count > SIZE_MAX / entry_size) {
                return false;
        }
        if (new_count != 0) {
                table = malloc(new_count * entry_size);
                if (table == NULL) {
                        return false;
                }
                for (i = 0; i < old_count; i++) {
                        if (i == remove_index) {
                                continue;
                        }
                        if (!executor_format_file_read(
                                file,
                                file->metatable.classes.offset + i * entry_size,
                                table +
                                    (i < remove_index ? i : i - 1) * entry_size,
                                entry_size)) {
                                free(table);
                                return false;
                        }
                }
        }
        if (!_append_table_bytes(file, table, entry_size, new_count,
                                 &table_offset)) {
                free(table);
                return false;
        }
        free(table);
        file->header.class_count       = (uint16_t)new_count;
        file->metatable.classes.offset = table_offset;
        file->metatable.classes.length = new_count * entry_size;
        return true;
}

bool _remove_interface_table_entry(struct executor_format_file* file,
                                   size_t remove_index) {
        size_t old_count  = file->header.interface_count;
        size_t new_count  = old_count - 1;
        size_t entry_size = EXECUTOR_FORMAT_INTERFACE_ENTRY_SIZE;
        uint8_t* table    = NULL;
        uint64_t table_offset;
        size_t i;

        if (remove_index >= old_count || old_count > SIZE_MAX / entry_size) {
                return false;
        }
        if (new_count != 0) {
                table = malloc(new_count * entry_size);
                if (table == NULL) {
                        return false;
                }
                for (i = 0; i < old_count; i++) {
                        if (i == remove_index) {
                                continue;
                        }
                        if (!executor_format_file_read(
                                file,
                                file->metatable.interfaces.offset +
                                    i * entry_size,
                                table +
                                    (i < remove_index ? i : i - 1) * entry_size,
                                entry_size)) {
                                free(table);
                                return false;
                        }
                }
        }
        if (!_append_table_bytes(file, table, entry_size, new_count,
                                 &table_offset)) {
                free(table);
                return false;
        }
        free(table);
        file->header.interface_count      = (uint16_t)new_count;
        file->metatable.interfaces.offset = table_offset;
        file->metatable.interfaces.length = new_count * entry_size;
        return true;
}

static bool _function_uses_class(
    struct executor_format_file* file,
    const struct executor_format_function_entry* entry, uint32_t class_id) {
        uint8_t* definition = NULL;
        size_t length       = entry->definition_length;
        size_t type_size;
        size_t offset;
        uint16_t name_length;
        uint16_t parameter_count;
        uint32_t expression_length;
        size_t i;
        bool uses = false;

        if (!_read_definition(file, entry->definition_offset,
                              entry->definition_length, &definition) ||
            length < 9) {
                free(definition);
                return true;
        }
        name_length     = executor_helper_read_u16(definition);
        parameter_count = executor_helper_read_u16(definition + 2);
        type_size       = definition[4] == EXECUTOR_FORMAT_TYPE_POINTER ? 5 : 1;
        if (definition[4] == EXECUTOR_FORMAT_TYPE_POINTER) {
                uses = executor_helper_read_u32(definition + 5) == class_id;
        }
        offset = 4 + type_size;
        if (offset > length || length - offset < 4) {
                free(definition);
                return true;
        }
        expression_length = executor_helper_read_u32(definition + offset);
        offset += 4;
        if (name_length > length - offset) {
                free(definition);
                return true;
        }
        offset += name_length;
        for (i = 0; i < parameter_count; i++) {
                uint8_t type_code;

                if (offset > length || length - offset < 3) {
                        uses = true;
                        break;
                }
                name_length = executor_helper_read_u16(definition + offset);
                type_code   = definition[offset + 2];
                offset += 3;
                if (type_code == EXECUTOR_FORMAT_TYPE_POINTER) {
                        if (length - offset < 4) {
                                uses = true;
                                break;
                        }
                        if (executor_helper_read_u32(definition + offset) ==
                            class_id) {
                                uses = true;
                        }
                        offset += 4;
                } else if (type_code < EXECUTOR_FORMAT_TYPE_INTEGER ||
                           type_code > EXECUTOR_FORMAT_TYPE_BOOL) {
                        uses = true;
                        break;
                }
                if (name_length > length - offset) {
                        uses = true;
                        break;
                }
                offset += name_length;
        }
        if (expression_length > length - offset) {
                uses = true;
        }
        free(definition);
        return uses;
}

bool _class_has_external_references(
    struct executor_format_file* file,
    const struct executor_format_class_entry* target) {
        size_t i;

        for (i = 0; i < file->header.class_count; i++) {
                struct executor_format_class_entry entry;
                struct executor_class_view view;
                size_t j;

                if (!executor_format_file_read_class_entry(file, i, &entry) ||
                    !_load_class_view(file, &entry, &view)) {
                        return true;
                }
                if (entry.class_id != target->class_id &&
                    view.parent_class_id == target->class_id) {
                        _class_view_free(&view);
                        return true;
                }
                if (entry.class_id != target->class_id) {
                        for (j = 0; j < view.field_count; j++) {
                                if (view.fields[j].type_code ==
                                        EXECUTOR_FORMAT_TYPE_POINTER &&
                                    view.fields[j].target_class_id ==
                                        target->class_id) {
                                        _class_view_free(&view);
                                        return true;
                                }
                        }
                }
                _class_view_free(&view);
        }
        for (i = 0; i < file->header.interface_count; i++) {
                struct executor_format_interface_entry entry;
                struct executor_interface_view view;
                size_t j;

                if (!executor_format_file_read_interface_entry(file, i,
                                                               &entry) ||
                    !_load_interface_view(file, &entry, &view)) {
                        return true;
                }
                for (j = 0; j < view.field_count; j++) {
                        if (view.fields[j].type_code ==
                                EXECUTOR_FORMAT_TYPE_POINTER &&
                            view.fields[j].target_class_id ==
                                target->class_id) {
                                _interface_view_free(&view);
                                return true;
                        }
                }
                _interface_view_free(&view);
        }
        for (i = 0; i < file->header.function_count; i++) {
                struct executor_format_function_entry entry;
                if (!executor_format_file_read_function_entry(file, i,
                                                              &entry) ||
                    entry.owner_class_id == target->class_id ||
                    _function_uses_class(file, &entry, target->class_id)) {
                        return true;
                }
        }
        return false;
}
