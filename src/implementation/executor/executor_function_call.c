/**
 * @file executor_function_call.c
 * @brief Executor function call implementation.
 */

#include "implementation/executor/executor_internal.h"

static void _function_view_free(struct executor_function_view* view) {
        free(view->parameters);
        free(view->bytes);
        memset(view, 0, sizeof *view);
}

static bool _load_function_view(
    struct executor_format_file* file,
    const struct executor_format_function_entry* entry,
    struct executor_function_view* view) {
        size_t type_size;
        size_t offset;
        size_t i;

        memset(view, 0, sizeof *view);
        if (!_read_definition(file, entry->definition_offset,
                              entry->definition_length, &view->bytes) ||
            entry->definition_length < 9) {
                _function_view_free(view);
                return false;
        }
        view->name_length      = executor_helper_read_u16(view->bytes);
        view->parameter_count  = executor_helper_read_u16(view->bytes + 2);
        view->return_type_code = view->bytes[4];
        if (view->return_type_code < EXECUTOR_FORMAT_TYPE_INTEGER ||
            view->return_type_code > EXECUTOR_FORMAT_TYPE_POINTER) {
                _function_view_free(view);
                return false;
        }
        type_size =
            view->return_type_code == EXECUTOR_FORMAT_TYPE_POINTER ? 5 : 1;
        if (type_size == 5) {
                view->return_target_class_id =
                    executor_helper_read_u32(view->bytes + 5);
        }
        offset = 4 + type_size;
        if (offset > entry->definition_length ||
            entry->definition_length - offset < 4) {
                _function_view_free(view);
                return false;
        }
        view->expression_length =
            executor_helper_read_u32(view->bytes + offset);
        offset += 4;
        if (view->name_length > entry->definition_length - offset) {
                _function_view_free(view);
                return false;
        }
        offset += view->name_length;
        view->parameters =
            calloc(view->parameter_count == 0 ? 1 : view->parameter_count,
                   sizeof *view->parameters);
        if (view->parameters == NULL) {
                _function_view_free(view);
                return false;
        }
        for (i = 0; i < view->parameter_count; i++) {
                struct executor_parameter_view* parameter =
                    &view->parameters[i];
                size_t parameter_type_size;
                if (offset > entry->definition_length ||
                    entry->definition_length - offset < 3) {
                        _function_view_free(view);
                        return false;
                }
                parameter->name_length =
                    executor_helper_read_u16(view->bytes + offset);
                parameter->type_code = view->bytes[offset + 2];
                offset += 3;
                if (parameter->type_code < EXECUTOR_FORMAT_TYPE_INTEGER ||
                    parameter->type_code > EXECUTOR_FORMAT_TYPE_POINTER) {
                        _function_view_free(view);
                        return false;
                }
                parameter_type_size =
                    parameter->type_code == EXECUTOR_FORMAT_TYPE_POINTER ? 4
                                                                         : 0;
                if (parameter_type_size > entry->definition_length - offset) {
                        _function_view_free(view);
                        return false;
                }
                if (parameter_type_size != 0) {
                        parameter->target_class_id =
                            executor_helper_read_u32(view->bytes + offset);
                        offset += parameter_type_size;
                }
                if (parameter->name_length >
                    entry->definition_length - offset) {
                        _function_view_free(view);
                        return false;
                }
                parameter->name = view->bytes + offset;
                offset += parameter->name_length;
        }
        if (view->expression_length > entry->definition_length - offset ||
            offset + view->expression_length != entry->definition_length) {
                _function_view_free(view);
                return false;
        }
        view->expression = view->bytes + offset;
        return true;
}

bool _class_name_by_id(struct executor_format_file* file, uint32_t class_id,
                       char** name) {
        struct executor_format_class_entry entry;
        struct executor_class_view view;
        char* copy;

        if (!_class_entry_by_id(file, class_id, &entry) ||
            !_load_class_view(file, &entry, &view)) {
                return false;
        }
        copy = malloc((size_t)view.name_length + 1);
        if (copy == NULL) {
                _class_view_free(&view);
                return false;
        }
        memcpy(copy, view.name, view.name_length);
        copy[view.name_length] = '\0';
        _class_view_free(&view);
        *name = copy;
        return true;
}

bool _print_value(struct executor_format_file* file,
                  const struct executor_value* value) {
        switch (value->type_code) {
                case EXECUTOR_FORMAT_TYPE_INTEGER:
                        return printf("%lld",
                                      (long long)value->value.integer) >= 0;
                case EXECUTOR_FORMAT_TYPE_FLOAT:
                        return printf("%.17g", value->value.floating) >= 0;
                case EXECUTOR_FORMAT_TYPE_TEXT:
                        if (putchar('"') == EOF ||
                            fwrite(value->value.text.bytes, 1,
                                   value->value.text.length,
                                   stdout) != value->value.text.length ||
                            putchar('"') == EOF) {
                                return false;
                        }
                        return true;
                case EXECUTOR_FORMAT_TYPE_BOOL:
                        return fputs(value->value.boolean ? "true" : "false",
                                     stdout) >= 0;
                case EXECUTOR_FORMAT_TYPE_POINTER: {
                        char* name = NULL;
                        bool ok;
                        if (!_class_name_by_id(
                                file, value->value.pointer.class_id, &name)) {
                                return false;
                        }
                        ok = printf("%s.%llu", name,
                                    (unsigned long long)
                                        value->value.pointer.object_id) >= 0;
                        free(name);
                        return ok;
                }
                default:
                        return false;
        }
}

enum EXECUTOR_RESULT _call_function(
    struct database* db, const struct analyzer_statement* statement) {
        struct executor_format_file file;
        struct executor_format_class_entry owner;
        struct executor_format_function_entry function_entry;
        struct executor_function_view function;
        struct executor_format_field* schema               = NULL;
        struct executor_format_object_field* object_fields = NULL;
        struct executor_binding* bindings                  = NULL;
        uint8_t** argument_bytes                           = NULL;
        uint8_t* object_segment                            = NULL;
        struct executor_value returned;
        uint64_t object_id;
        size_t schema_count = 0;
        size_t i;
        bool loaded_function        = false;
        enum EXECUTOR_RESULT result = EXECUTOR_ERROR_BAD_STATEMENT;

        memset(&function, 0, sizeof function);
        if (statement->name == NULL || statement->function == NULL ||
            statement->id == NULL || statement->values.items == NULL ||
            !_parse_u64(statement->id, &object_id) || object_id == 0 ||
            !db->format_mode || !executor_format_file_load(db, &file)) {
                goto done;
        }
        if (!_find_class(&file, statement->name, &owner)) {
                goto done;
        }
        if (!_find_function(&file, owner.class_id, statement->function, NULL,
                            &function_entry)) {
                goto done;
        }
        if (!_load_function_view(&file, &function_entry, &function)) {
                goto done;
        }
        loaded_function = true;
        if (statement->values.count != function.parameter_count ||
            !_collect_schema_fields(&file, owner.class_id, &schema,
                                    &schema_count) ||
            !_load_object_fields(&file, &owner, object_id, schema, schema_count,
                                 &object_segment, NULL, NULL, &object_fields)) {
                goto done;
        }
        bindings =
            calloc(function.parameter_count == 0 ? 1 : function.parameter_count,
                   sizeof *bindings);
        argument_bytes =
            calloc(function.parameter_count == 0 ? 1 : function.parameter_count,
                   sizeof *argument_bytes);
        if (bindings == NULL || argument_bytes == NULL) {
                result = EXECUTOR_ERROR_STORAGE_FAILED;
                goto done;
        }
        for (i = 0; i < function.parameter_count; i++) {
                const struct executor_parameter_view* parameter =
                    &function.parameters[i];
                struct executor_format_field parameter_field;
                uint32_t value_length;
                size_t j;

                memset(&parameter_field, 0, sizeof parameter_field);
                parameter_field.type_code       = parameter->type_code;
                parameter_field.target_class_id = parameter->target_class_id;
                if (!_encode_input_value(&file, &parameter_field,
                                         &statement->values.items[i],
                                         &argument_bytes[i], &value_length) ||
                    !_value_from_bytes(parameter->type_code, argument_bytes[i],
                                       value_length, &bindings[i].value)) {
                        goto done;
                }
                for (j = 0; j < i; j++) {
                        if (bindings[j].name_length == parameter->name_length &&
                            memcmp(bindings[j].name, parameter->name,
                                   parameter->name_length) == 0) {
                                goto done;
                        }
                }
                bindings[i].name        = parameter->name;
                bindings[i].name_length = parameter->name_length;
        }
        {
                struct executor_eval_context context = {
                    .schema        = schema,
                    .schema_count  = schema_count,
                    .fields        = object_fields,
                    .bindings      = bindings,
                    .binding_count = function.parameter_count,
                };
                if (!_evaluate_text_expression(function.expression,
                                               function.expression_length,
                                               &context, &returned) ||
                    returned.type_code != function.return_type_code ||
                    (returned.type_code == EXECUTOR_FORMAT_TYPE_POINTER &&
                     returned.value.pointer.class_id !=
                         function.return_target_class_id) ||
                    !_print_value(&file, &returned) || putchar('\n') == EOF) {
                        goto done;
                }
        }
        result = EXECUTOR_OK;
done:
        if (argument_bytes != NULL) {
                for (i = 0;
                     i < (loaded_function ? function.parameter_count : 0);
                     i++) {
                        free(argument_bytes[i]);
                }
        }
        free(argument_bytes);
        free(bindings);
        free(object_segment);
        _free_object_fields(object_fields, schema_count);
        _free_schema_fields(schema, schema_count);
        if (loaded_function) {
                _function_view_free(&function);
        }
        return result;
}
