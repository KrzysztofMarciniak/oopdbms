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
    struct database* db, const struct analyzer_statement* statement,
    FILE* output) {
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
        if (fprintf(output, "%s\n", statement->name) < 0) {
                _class_view_free(&view);
                _free_schema_fields(schema, schema_count);
                return EXECUTOR_ERROR_OUTPUT_FAILED;
        }
        if (view.parent_class_id != 0) {
                char* parent_name = NULL;
                if (!_class_name_by_id(&file, view.parent_class_id,
                                       &parent_name)) {
                        _class_view_free(&view);
                        _free_schema_fields(schema, schema_count);
                        return EXECUTOR_ERROR_STORAGE_FAILED;
                }
                if (fprintf(output, "  EXTENDS %s\n", parent_name) < 0) {
                        free(parent_name);
                        _class_view_free(&view);
                        _free_schema_fields(schema, schema_count);
                        return EXECUTOR_ERROR_OUTPUT_FAILED;
                }
                free(parent_name);
        }
        for (i = 0; i < schema_count; i++) {
                char type[UINT16_MAX + 16];
                if (!_type_name(&file, &schema[i], type, sizeof type)) {
                        _class_view_free(&view);
                        _free_schema_fields(schema, schema_count);
                        return EXECUTOR_ERROR_STORAGE_FAILED;
                }
                if (fprintf(output, "  %s %s\n", schema[i].name, type) < 0) {
                        _class_view_free(&view);
                        _free_schema_fields(schema, schema_count);
                        return EXECUTOR_ERROR_OUTPUT_FAILED;
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
                if (fprintf(output, "  IMPLEMENTS %s\n", interface_name) < 0) {
                        free(interface_name);
                        _class_view_free(&view);
                        _free_schema_fields(schema, schema_count);
                        return EXECUTOR_ERROR_OUTPUT_FAILED;
                }
                free(interface_name);
        }
        _class_view_free(&view);
        _free_schema_fields(schema, schema_count);
        return EXECUTOR_OK;
}

static enum EXECUTOR_RESULT _describe_interface(
    struct database* db, const struct analyzer_statement* statement,
    FILE* output) {
        struct executor_format_file file;
        struct executor_format_interface_entry entry;
        struct executor_interface_view view;
        size_t index;

        if (statement->name == NULL || !db->format_mode ||
            !executor_format_file_load(db, &file) ||
            !_find_interface(&file, statement->name, &entry)) {
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }
        if (!_load_interface_view(&file, &entry, &view)) {
                return EXECUTOR_ERROR_STORAGE_FAILED;
        }
        if (fprintf(output, "%s\n", statement->name) < 0) {
                _interface_view_free(&view);
                return EXECUTOR_ERROR_OUTPUT_FAILED;
        }
        for (index = 0; index < view.field_count; index++) {
                const struct executor_format_field* field = &view.fields[index];
                char type[UINT16_MAX + 16];

                if (!_type_name(&file, field, type, sizeof type)) {
                        _interface_view_free(&view);
                        return EXECUTOR_ERROR_STORAGE_FAILED;
                }
                if (fputs("  ", output) < 0 ||
                    fwrite(field->name, 1, field->name_length, output) !=
                        field->name_length ||
                    fputc(' ', output) == EOF || fputs(type, output) < 0 ||
                    fputc('\n', output) == EOF) {
                        _interface_view_free(&view);
                        return EXECUTOR_ERROR_OUTPUT_FAILED;
                }
        }
        _interface_view_free(&view);
        return EXECUTOR_OK;
}

static enum EXECUTOR_RESULT _list_entries(struct database* db, bool classes,
                                          FILE* output) {
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
                                      definition_length, &definition)) {
                        return EXECUTOR_ERROR_STORAGE_FAILED;
                }
                if (definition_length < name_offset) {
                        free(definition);
                        return EXECUTOR_ERROR_STORAGE_FAILED;
                }
                name_length =
                    executor_helper_read_u16(definition + name_header_offset);
                if ((size_t)name_length > definition_length - name_offset) {
                        free(definition);
                        return EXECUTOR_ERROR_STORAGE_FAILED;
                }
                if (fwrite(definition + name_offset, 1, name_length, output) !=
                        name_length ||
                    fputc('\n', output) == EOF) {
                        free(definition);
                        return EXECUTOR_ERROR_OUTPUT_FAILED;
                }
                free(definition);
        }
        return EXECUTOR_OK;
}

static bool _function_type_name(struct executor_format_file* file,
                                uint8_t type_code, uint32_t target_class_id,
                                char* buffer, size_t capacity) {
        struct executor_format_field field = {
            .type_code       = type_code,
            .target_class_id = target_class_id,
        };

        return _type_name(file, &field, buffer, capacity);
}

static enum EXECUTOR_RESULT _list_functions(struct database* db, FILE* output) {
        struct executor_format_file file;
        size_t index;

        if (!db->format_mode || !executor_format_file_load(db, &file)) {
                return EXECUTOR_ERROR_STORAGE_FAILED;
        }
        for (index = 0; index < file.header.function_count; index++) {
                struct executor_format_function_entry entry;
                struct executor_function_view view;
                char* owner_name = NULL;
                bool loaded;

                if (!executor_format_file_read_function_entry(&file, index,
                                                              &entry)) {
                        return EXECUTOR_ERROR_STORAGE_FAILED;
                }
                loaded = _load_function_view(&file, &entry, &view);
                if (!loaded || !_class_name_by_id(&file, entry.owner_class_id,
                                                  &owner_name)) {
                        if (loaded) {
                                _function_view_free(&view);
                        }
                        free(owner_name);
                        return EXECUTOR_ERROR_STORAGE_FAILED;
                }
                if (fprintf(output, "%s.", owner_name) < 0 ||
                    fwrite(view.name, 1, view.name_length, output) !=
                        view.name_length ||
                    fputc('\n', output) == EOF) {
                        free(owner_name);
                        _function_view_free(&view);
                        return EXECUTOR_ERROR_OUTPUT_FAILED;
                }
                free(owner_name);
                _function_view_free(&view);
        }
        return EXECUTOR_OK;
}

static enum EXECUTOR_RESULT _describe_function(
    struct database* db, const struct analyzer_statement* statement,
    FILE* output) {
        struct executor_format_file file;
        struct executor_format_class_entry owner;
        struct executor_format_function_entry entry;
        struct executor_function_view view;
        enum EXECUTOR_RESULT result = EXECUTOR_ERROR_BAD_STATEMENT;
        size_t index;

        memset(&view, 0, sizeof view);
        if (statement->name == NULL || statement->function == NULL ||
            !db->format_mode || !executor_format_file_load(db, &file) ||
            !_find_class(&file, statement->name, &owner) ||
            !_find_function(&file, owner.class_id, statement->function, NULL,
                            &entry)) {
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }
        if (!_load_function_view(&file, &entry, &view)) {
                return EXECUTOR_ERROR_STORAGE_FAILED;
        }
        result = EXECUTOR_ERROR_OUTPUT_FAILED;
        if (fprintf(output, "%s.", statement->name) < 0 ||
            fwrite(view.name, 1, view.name_length, output) !=
                view.name_length ||
            fputs(" (", output) < 0) {
                goto done;
        }
        for (index = 0; index < view.parameter_count; index++) {
                const struct executor_parameter_view* parameter =
                    &view.parameters[index];
                char type[UINT16_MAX + 16];

                if ((index != 0 && fputs(", ", output) < 0) ||
                    fwrite(parameter->name, 1, parameter->name_length,
                           output) != parameter->name_length ||
                    fputc(' ', output) == EOF) {
                        goto done;
                }
                if (!_function_type_name(&file, parameter->type_code,
                                         parameter->target_class_id, type,
                                         sizeof type)) {
                        result = EXECUTOR_ERROR_STORAGE_FAILED;
                        goto done;
                }
                if (fputs(type, output) < 0) {
                        goto done;
                }
        }
        {
                char return_type[UINT16_MAX + 16];

                if (fputs(") RETURNS ", output) < 0) {
                        goto done;
                }
                if (!_function_type_name(&file, view.return_type_code,
                                         view.return_target_class_id,
                                         return_type, sizeof return_type)) {
                        result = EXECUTOR_ERROR_STORAGE_FAILED;
                        goto done;
                }
                if (fputs(return_type, output) < 0 ||
                    fputs(" AS ", output) < 0 ||
                    fwrite(view.expression, 1, view.expression_length,
                           output) != view.expression_length ||
                    fputc('\n', output) == EOF) {
                        goto done;
                }
        }
        result = EXECUTOR_OK;
done:
        _function_view_free(&view);
        return result;
}

enum EXECUTOR_RESULT executor_metadata_execute(
    struct database* db, const struct analyzer_statement* statement,
    FILE* output) {
        switch (statement->kind) {
                case ANALYZER_LIST_CLASSES:
                        return _list_entries(db, true, output);
                case ANALYZER_LIST_INTERFACES:
                        return _list_entries(db, false, output);
                case ANALYZER_LIST_FUNCTIONS:
                        return _list_functions(db, output);
                case ANALYZER_DESCRIBE:
                        return _describe_class(db, statement, output);
                case ANALYZER_DESCRIBE_FUNCTION:
                        return _describe_function(db, statement, output);
                case ANALYZER_DESCRIBE_INTERFACE:
                        return _describe_interface(db, statement, output);
                default:
                        return EXECUTOR_ERROR_NOT_IMPLEMENTED;
        }
}
