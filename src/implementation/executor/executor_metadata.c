/**
 * @file executor_metadata.c
 * @brief Executor metadata implementation.
 */

#include "implementation/executor/executor_internal.h"

static bool _type_name(struct executor_format_file* file,
                       const struct executor_format_field* field, char* buffer,
                       size_t capacity) {
        int result;
        if (field->type_code == EXECUTOR_FORMAT_TYPE_POINTER) {
                char* class_name = NULL;
                if (!_class_name_by_id(file, field->target_class_id,
                                       &class_name)) {
                        return false;
                }
                result = snprintf(buffer, capacity, "pointer %s", class_name);
                free(class_name);
        } else {
                static const char* const names[] = {"invalid", "integer",
                                                    "float", "text", "bool"};
                if (field->type_code > EXECUTOR_FORMAT_TYPE_BOOL) {
                        return false;
                }
                result =
                    snprintf(buffer, capacity, "%s", names[field->type_code]);
        }
        return result >= 0 && (size_t)result < capacity;
}

static enum EXECUTOR_RESULT _describe_class(
    struct database* db, const struct analyzer_statement* statement) {
        struct executor_format_file file;
        struct executor_format_class_entry entry;
        struct executor_class_view view;
        struct executor_format_field* schema = NULL;
        size_t schema_count                  = 0;
        size_t i;

        if (statement->name == NULL || !db->format_mode ||
            !executor_format_file_load(db, &file) ||
            !_find_class(&file, statement->name, &entry)) {
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }
        if (!_load_class_view(&file, &entry, &view) ||
            !_collect_schema_fields(&file, entry.class_id, &schema,
                                    &schema_count)) {
                _class_view_free(&view);
                _free_schema_fields(schema, schema_count);
                return EXECUTOR_ERROR_STORAGE_FAILED;
        }
        if (printf("%s\n", statement->name) < 0) {
                _class_view_free(&view);
                _free_schema_fields(schema, schema_count);
                return EXECUTOR_ERROR_STORAGE_FAILED;
        }
        if (view.parent_class_id != 0) {
                char* parent_name = NULL;
                if (!_class_name_by_id(&file, view.parent_class_id,
                                       &parent_name) ||
                    printf("  EXTENDS %s\n", parent_name) < 0) {
                        free(parent_name);
                        _class_view_free(&view);
                        _free_schema_fields(schema, schema_count);
                        return EXECUTOR_ERROR_STORAGE_FAILED;
                }
                free(parent_name);
        }
        for (i = 0; i < schema_count; i++) {
                char type[UINT16_MAX + 16];
                if (!_type_name(&file, &schema[i], type, sizeof type) ||
                    printf("  %s %s\n", schema[i].name, type) < 0) {
                        _class_view_free(&view);
                        _free_schema_fields(schema, schema_count);
                        return EXECUTOR_ERROR_STORAGE_FAILED;
                }
        }
        for (i = 0; i < view.interface_count; i++) {
                struct executor_format_interface_entry interface_entry;
                struct executor_interface_view interface_view;
                char* interface_name;
                if (!_interface_entry_by_id(&file, view.interface_ids[i],
                                            &interface_entry) ||
                    !_load_interface_view(&file, &interface_entry,
                                          &interface_view)) {
                        _class_view_free(&view);
                        _free_schema_fields(schema, schema_count);
                        return EXECUTOR_ERROR_STORAGE_FAILED;
                }
                interface_name = malloc((size_t)interface_view.name_length + 1);
                if (interface_name == NULL) {
                        _interface_view_free(&interface_view);
                        _class_view_free(&view);
                        _free_schema_fields(schema, schema_count);
                        return EXECUTOR_ERROR_STORAGE_FAILED;
                }
                memcpy(interface_name, interface_view.bytes + 4,
                       interface_view.name_length);
                interface_name[interface_view.name_length] = '\0';
                _interface_view_free(&interface_view);
                if (printf("  IMPLEMENTS %s\n", interface_name) < 0) {
                        free(interface_name);
                        _class_view_free(&view);
                        _free_schema_fields(schema, schema_count);
                        return EXECUTOR_ERROR_STORAGE_FAILED;
                }
                free(interface_name);
        }
        _class_view_free(&view);
        _free_schema_fields(schema, schema_count);
        return EXECUTOR_OK;
}

static enum EXECUTOR_RESULT _list_entries(struct database* db, bool classes) {
        struct executor_format_file file;
        size_t count;
        size_t index;

        if (!db->format_mode || !executor_format_file_load(db, &file)) {
                return EXECUTOR_ERROR_STORAGE_FAILED;
        }
        count = classes ? file.header.class_count : file.header.interface_count;
        for (index = 0; index < count; index++) {
                uint64_t definition_offset;
                uint32_t definition_length;
                uint8_t* definition;
                uint16_t name_length;
                size_t name_header_offset;
                size_t name_offset;

                if (classes) {
                        struct executor_format_class_entry entry;
                        if (!executor_format_file_read_class_entry(&file, index,
                                                                   &entry)) {
                                return EXECUTOR_ERROR_STORAGE_FAILED;
                        }
                        definition_offset  = entry.definition_offset;
                        definition_length  = entry.definition_length;
                        name_header_offset = 0;
                        name_offset        = 16;
                } else {
                        struct executor_format_interface_entry entry;
                        if (!executor_format_file_read_interface_entry(
                                &file, index, &entry)) {
                                return EXECUTOR_ERROR_STORAGE_FAILED;
                        }
                        definition_offset  = entry.definition_offset;
                        definition_length  = entry.definition_length;
                        name_header_offset = 0;
                        name_offset        = 4;
                }
                if (!_read_definition(&file, definition_offset,
                                      definition_length, &definition) ||
                    definition_length < name_offset) {
                        return EXECUTOR_ERROR_STORAGE_FAILED;
                }
                name_length =
                    executor_helper_read_u16(definition + name_header_offset);
                if ((size_t)name_length > definition_length - name_offset) {
                        free(definition);
                        return EXECUTOR_ERROR_STORAGE_FAILED;
                }
                if (fwrite(definition + name_offset, 1, name_length, stdout) !=
                        name_length ||
                    fputc('\n', stdout) == EOF) {
                        free(definition);
                        return EXECUTOR_ERROR_STORAGE_FAILED;
                }
                free(definition);
        }
        return EXECUTOR_OK;
}

enum EXECUTOR_RESULT executor_metadata_execute(
    struct database* db, const struct analyzer_statement* statement) {
        switch (statement->kind) {
                case ANALYZER_LIST_CLASSES:
                        return _list_entries(db, true);
                case ANALYZER_LIST_INTERFACES:
                        return _list_entries(db, false);
                case ANALYZER_DESCRIBE:
                        return _describe_class(db, statement);
                default:
                        return EXECUTOR_ERROR_NOT_IMPLEMENTED;
        }
}
