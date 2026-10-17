/**
 * @file executor_function_definitions.c
 * @brief Executor function definitions implementation.
 */

#include "implementation/executor/executor_internal.h"

static bool _validate_function_expression(
    struct executor_format_file* file, uint32_t owner_class_id,
    const struct executor_format_parameter* parameters, size_t parameter_count,
    const uint8_t* expression, size_t expression_length,
    uint8_t return_type_code, uint32_t return_target_class_id);

static bool _expression_text_append(struct expression_text* text,
                                    const char* bytes, size_t length) {
        char* grown;
        size_t needed;
        size_t capacity;

        if (length > SIZE_MAX - text->length - 1) {
                return false;
        }
        needed = text->length + length + 1;
        if (needed > text->capacity) {
                capacity = text->capacity == 0 ? 64 : text->capacity;
                while (capacity < needed) {
                        if (capacity > SIZE_MAX / 2) {
                                capacity = needed;
                                break;
                        }
                        capacity *= 2;
                }
                grown = realloc(text->bytes, capacity);
                if (grown == NULL) {
                        return false;
                }
                text->bytes    = grown;
                text->capacity = capacity;
        }
        memcpy(text->bytes + text->length, bytes, length);
        text->length += length;
        text->bytes[text->length] = '\0';
        return true;
}

static bool _expression_to_text(const struct analyzer_expression* expression,
                                struct expression_text* output) {
        static const char* const operators[] = {"+",  "-", "*",  "/", "=",
                                                "!=", "<", "<=", ">", ">="};
        size_t i;

        if (expression == NULL) {
                return false;
        }
        switch (expression->kind) {
                case ANALYZER_EXPRESSION_LITERAL:
                        if (expression->value.kind == ANALYZER_VALUE_TEXT) {
                                if (!_expression_text_append(output, "\"", 1)) {
                                        return false;
                                }
                                for (i = 0; expression->value.text[i] != '\0';
                                     i++) {
                                        char ch = expression->value.text[i];
                                        if ((ch == '"' || ch == '\\') &&
                                            !_expression_text_append(output,
                                                                     "\\", 1)) {
                                                return false;
                                        }
                                        if (!_expression_text_append(output,
                                                                     &ch, 1)) {
                                                return false;
                                        }
                                }
                                return _expression_text_append(output, "\"", 1);
                        }
                        return expression->value.text != NULL &&
                               _expression_text_append(
                                   output, expression->value.text,
                                   strlen(expression->value.text));
                case ANALYZER_EXPRESSION_FIELD:
                        return expression->field != NULL &&
                               _expression_text_append(
                                   output, expression->field,
                                   strlen(expression->field));
                case ANALYZER_EXPRESSION_NEGATE:
                        return _expression_text_append(output, "-(", 2) &&
                               _expression_to_text(expression->left, output) &&
                               _expression_text_append(output, ")", 1);
                case ANALYZER_EXPRESSION_BINARY:
                        if ((size_t)expression->op >=
                                sizeof operators / sizeof operators[0] ||
                            !_expression_text_append(output, "(", 1) ||
                            !_expression_to_text(expression->left, output) ||
                            !_expression_text_append(
                                output, operators[expression->op],
                                strlen(operators[expression->op])) ||
                            !_expression_to_text(expression->right, output)) {
                                return false;
                        }
                        return _expression_text_append(output, ")", 1);
                default:
                        return false;
        }
}

static bool _function_name_is(const uint8_t* definition, size_t length,
                              const char* name) {
        size_t type_size;
        size_t name_offset;
        uint16_t name_length;

        if (definition == NULL || length < 9 || name == NULL ||
            definition[4] < EXECUTOR_FORMAT_TYPE_INTEGER ||
            definition[4] > EXECUTOR_FORMAT_TYPE_POINTER) {
                return false;
        }
        type_size   = definition[4] == EXECUTOR_FORMAT_TYPE_POINTER ? 5 : 1;
        name_offset = 4 + type_size + 4;
        name_length = executor_helper_read_u16(definition);
        if (name_offset > length || name_length > length - name_offset) {
                return false;
        }
        return name_length == strlen(name) &&
               memcmp(definition + name_offset, name, name_length) == 0;
}

bool _find_function(struct executor_format_file* file, uint32_t owner_class_id,
                    const char* function_name, size_t* found_index,
                    struct executor_format_function_entry* found) {
        size_t i;

        for (i = 0; i < file->header.function_count; i++) {
                struct executor_format_function_entry entry;
                uint8_t* definition;
                bool matches;

                if (!executor_format_file_read_function_entry(file, i,
                                                              &entry) ||
                    !_read_definition(file, entry.definition_offset,
                                      entry.definition_length, &definition)) {
                        return false;
                }
                matches = entry.owner_class_id == owner_class_id &&
                          _function_name_is(definition, entry.definition_length,
                                            function_name);
                free(definition);
                if (matches) {
                        if (found_index != NULL) {
                                *found_index = i;
                        }
                        if (found != NULL) {
                                *found = entry;
                        }
                        return true;
                }
        }
        return false;
}

static bool _max_function_id(struct executor_format_file* file,
                             uint32_t* max_id) {
        size_t i;

        *max_id = 0;
        for (i = 0; i < file->header.function_count; i++) {
                struct executor_format_function_entry entry;
                if (!executor_format_file_read_function_entry(file, i,
                                                              &entry)) {
                        return false;
                }
                if (entry.function_id > *max_id) {
                        *max_id = entry.function_id;
                }
        }
        return true;
}

static bool _function_type_target(struct executor_format_file* file,
                                  const struct analyzer_type* type,
                                  uint8_t* type_code, uint32_t* target_id) {
        struct executor_format_class_entry target;

        *type_code = _format_type(type);
        *target_id = 0;
        if (*type_code == 0) {
                return false;
        }
        if (*type_code != EXECUTOR_FORMAT_TYPE_POINTER) {
                return true;
        }
        if (type->class_name == NULL ||
            !_find_class(file, type->class_name, &target)) {
                return false;
        }
        *target_id = target.class_id;
        return true;
}

static enum EXECUTOR_RESULT _create_function(
    struct database* db, const struct analyzer_statement* statement) {
        struct executor_format_file file;
        struct executor_format_class_entry owner;
        struct executor_format_function_entry entry;
        struct executor_format_function_definition definition;
        struct executor_format_parameter* parameters = NULL;
        struct expression_text expression            = {0};
        uint8_t* definition_bytes                    = NULL;
        uint8_t entry_bytes[EXECUTOR_FORMAT_FUNCTION_ENTRY_SIZE];
        uint32_t max_id;
        uint64_t definition_offset;
        uint64_t table_offset;
        size_t definition_capacity;
        size_t definition_length;
        size_t table_length;
        size_t i;
        enum EXECUTOR_RESULT result = EXECUTOR_ERROR_BAD_STATEMENT;

        if (statement->name == NULL || statement->function == NULL ||
            statement->name[0] == '\0' || statement->function[0] == '\0' ||
            strlen(statement->function) > UINT16_MAX ||
            statement->fields.count > UINT16_MAX ||
            statement->fields.items == NULL || statement->expression == NULL ||
            !db->format_mode || !executor_format_file_load(db, &file)) {
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }
        if (file.header.function_count == UINT16_MAX ||
            !_find_class(&file, statement->name, &owner) ||
            !_max_function_id(&file, &max_id) ||
            !_next_id(max_id, &entry.function_id)) {
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }
        if (_find_function(&file, owner.class_id, statement->function, NULL,
                           NULL)) {
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }
        if (statement->fields.count != 0) {
                parameters =
                    calloc(statement->fields.count, sizeof *parameters);
                if (parameters == NULL) {
                        return EXECUTOR_ERROR_STORAGE_FAILED;
                }
        }
        for (i = 0; i < statement->fields.count; i++) {
                const struct analyzer_field* source =
                    &statement->fields.items[i];
                size_t prior;

                if (source->name == NULL || source->name[0] == '\0' ||
                    strlen(source->name) > UINT16_MAX ||
                    !_function_type_target(&file, &source->type,
                                           &parameters[i].type_code,
                                           &parameters[i].target_class_id)) {
                        goto done;
                }
                for (prior = 0; prior < i; prior++) {
                        if (strcmp(source->name,
                                   statement->fields.items[prior].name) == 0) {
                                goto done;
                        }
                }
                parameters[i].name        = (const uint8_t*)source->name;
                parameters[i].name_length = (uint16_t)strlen(source->name);
        }
        if (!_expression_to_text(statement->expression, &expression) ||
            expression.length > UINT32_MAX) {
                goto done;
        }
        memset(&definition, 0, sizeof definition);
        definition.name              = (const uint8_t*)statement->function;
        definition.name_length       = (uint16_t)strlen(statement->function);
        definition.parameters        = parameters;
        definition.parameter_count   = (uint16_t)statement->fields.count;
        definition.expression        = (const uint8_t*)expression.bytes;
        definition.expression_length = (uint32_t)expression.length;
        if (!_function_type_target(&file, &statement->return_type,
                                   &definition.return_type_code,
                                   &definition.return_target_class_id)) {
                goto done;
        }
        if (!_validate_function_expression(
                &file, owner.class_id, parameters, statement->fields.count,
                (const uint8_t*)expression.bytes, expression.length,
                definition.return_type_code,
                definition.return_target_class_id)) {
                goto done;
        }
        definition_capacity =
            9 + definition.name_length + definition.expression_length;
        for (i = 0; i < statement->fields.count; i++) {
                size_t parameter_size =
                    2 +
                    (parameters[i].type_code == EXECUTOR_FORMAT_TYPE_POINTER
                         ? 5
                         : 1) +
                    parameters[i].name_length;
                if (parameter_size > SIZE_MAX - definition_capacity) {
                        goto done;
                }
                definition_capacity += parameter_size;
        }
        if (definition.return_type_code == EXECUTOR_FORMAT_TYPE_POINTER) {
                definition_capacity += 4;
        }
        definition_bytes = malloc(definition_capacity);
        if (definition_bytes == NULL ||
            !executor_helper_create_function_definition(
                definition_bytes, definition_capacity, &definition,
                &definition_length) ||
            definition_length > UINT32_MAX ||
            !_next_id(max_id, &entry.function_id)) {
                result = EXECUTOR_ERROR_STORAGE_FAILED;
                goto done;
        }
        entry.owner_class_id    = owner.class_id;
        entry.definition_length = (uint32_t)definition_length;
        table_length            = ((size_t)file.header.function_count + 1) *
                                  EXECUTOR_FORMAT_FUNCTION_ENTRY_SIZE;
        if (file.size > UINT64_MAX - table_length) {
                result = EXECUTOR_ERROR_STORAGE_FAILED;
                goto done;
        }
        entry.definition_offset = file.size + table_length;
        if (!executor_helper_create_function_entry(
                entry_bytes, sizeof entry_bytes, &entry) ||
            !_append_new_table(&file, &file.metatable.functions,
                               file.header.function_count,
                               EXECUTOR_FORMAT_FUNCTION_ENTRY_SIZE, entry_bytes,
                               &table_offset) ||
            table_offset != file.size - table_length ||
            !executor_format_file_append(&file, definition_bytes,
                                         definition_length,
                                         &definition_offset) ||
            definition_offset != entry.definition_offset) {
                result = EXECUTOR_ERROR_STORAGE_FAILED;
                goto done;
        }
        file.metatable.functions.offset = table_offset;
        file.metatable.functions.length = table_length;
        file.header.function_count++;
        result = executor_format_file_commit(&file)
                     ? EXECUTOR_OK
                     : EXECUTOR_ERROR_SAVE_FAILED;
done:
        free(definition_bytes);
        free(parameters);
        free(expression.bytes);
        return result;
}

static enum EXECUTOR_RESULT _drop_function(
    struct database* db, const struct analyzer_statement* statement) {
        struct executor_format_file file;
        struct executor_format_class_entry owner;
        size_t index;
        size_t old_count;
        size_t new_count;
        size_t entry_size = EXECUTOR_FORMAT_FUNCTION_ENTRY_SIZE;
        uint8_t* entries;
        uint64_t new_offset = 0;
        size_t i;

        if (statement->name == NULL || statement->function == NULL ||
            !db->format_mode || !executor_format_file_load(db, &file) ||
            !_find_class(&file, statement->name, &owner) ||
            !_find_function(&file, owner.class_id, statement->function, &index,
                            NULL)) {
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }
        old_count = file.header.function_count;
        new_count = old_count - 1;
        entries   = malloc(new_count == 0 ? 1 : new_count * entry_size);
        if (entries == NULL) {
                return EXECUTOR_ERROR_STORAGE_FAILED;
        }
        for (i = 0; i < old_count; i++) {
                if (i < index) {
                        if (!executor_format_file_read(
                                &file,
                                file.metatable.functions.offset +
                                    i * entry_size,
                                entries + i * entry_size, entry_size)) {
                                free(entries);
                                return EXECUTOR_ERROR_STORAGE_FAILED;
                        }
                } else if (i > index) {
                        if (!executor_format_file_read(
                                &file,
                                file.metatable.functions.offset +
                                    i * entry_size,
                                entries + (i - 1) * entry_size, entry_size)) {
                                free(entries);
                                return EXECUTOR_ERROR_STORAGE_FAILED;
                        }
                }
        }
        if (new_count != 0 &&
            !executor_format_file_append(&file, entries, new_count * entry_size,
                                         &new_offset)) {
                free(entries);
                return EXECUTOR_ERROR_STORAGE_FAILED;
        }
        free(entries);
        file.header.function_count      = (uint16_t)new_count;
        file.metatable.functions.offset = new_offset;
        file.metatable.functions.length = new_count * entry_size;
        return executor_format_file_commit(&file) ? EXECUTOR_OK
                                                  : EXECUTOR_ERROR_SAVE_FAILED;
}

static bool _validate_function_expression(
    struct executor_format_file* file, uint32_t owner_class_id,
    const struct executor_format_parameter* parameters, size_t parameter_count,
    const uint8_t* expression, size_t expression_length,
    uint8_t return_type_code, uint32_t return_target_class_id) {
        struct executor_format_field* schema        = NULL;
        struct executor_format_object_field* fields = NULL;
        struct executor_binding* bindings           = NULL;
        uint8_t** values                            = NULL;
        struct executor_value result;
        struct executor_eval_context context;
        size_t schema_count = 0;
        size_t i;
        bool valid = false;

        if (!_collect_schema_fields(file, owner_class_id, &schema,
                                    &schema_count) ||
            schema_count > SIZE_MAX / sizeof *fields ||
            parameter_count > SIZE_MAX / sizeof *bindings) {
                goto done;
        }
        fields   = calloc(schema_count == 0 ? 1 : schema_count, sizeof *fields);
        values   = calloc(schema_count == 0 ? 1 : schema_count, sizeof *values);
        bindings = calloc(parameter_count == 0 ? 1 : parameter_count,
                          sizeof *bindings);
        if (fields == NULL || values == NULL || bindings == NULL) {
                goto done;
        }
        for (i = 0; i < schema_count; i++) {
                uint8_t bytes[12] = {0};
                size_t length     = 0;

                fields[i].field_id  = schema[i].field_id;
                fields[i].type_code = schema[i].type_code;
                switch (schema[i].type_code) {
                        case EXECUTOR_FORMAT_TYPE_INTEGER:
                                executor_helper_write_u64(bytes, 1);
                                length = 8;
                                break;
                        case EXECUTOR_FORMAT_TYPE_FLOAT:
                                if (!executor_helper_create_float_value(
                                        bytes, sizeof bytes, 1.0, &length)) {
                                        goto done;
                                }
                                break;
                        case EXECUTOR_FORMAT_TYPE_TEXT:
                                length = 0;
                                break;
                        case EXECUTOR_FORMAT_TYPE_BOOL:
                                if (!executor_helper_create_bool_value(
                                        bytes, sizeof bytes, true, &length)) {
                                        goto done;
                                }
                                break;
                        case EXECUTOR_FORMAT_TYPE_POINTER:
                                executor_helper_write_u32(
                                    bytes, schema[i].target_class_id);
                                executor_helper_write_u64(bytes + 4, 1);
                                length = 12;
                                break;
                        default:
                                goto done;
                }
                if (length != 0) {
                        values[i] = malloc(length);
                        if (values[i] == NULL) {
                                goto done;
                        }
                        memcpy(values[i], bytes, length);
                }
                fields[i].value        = values[i];
                fields[i].value_length = (uint32_t)length;
        }
        for (i = 0; i < parameter_count; i++) {
                bindings[i].name            = parameters[i].name;
                bindings[i].name_length     = parameters[i].name_length;
                bindings[i].value.type_code = parameters[i].type_code;
                switch (parameters[i].type_code) {
                        case EXECUTOR_FORMAT_TYPE_INTEGER:
                                bindings[i].value.value.integer = 1;
                                break;
                        case EXECUTOR_FORMAT_TYPE_FLOAT:
                                bindings[i].value.value.floating = 1.0;
                                break;
                        case EXECUTOR_FORMAT_TYPE_TEXT:
                                bindings[i].value.value.text.bytes =
                                    (const uint8_t*)"";
                                bindings[i].value.value.text.length = 0;
                                break;
                        case EXECUTOR_FORMAT_TYPE_BOOL:
                                bindings[i].value.value.boolean = true;
                                break;
                        case EXECUTOR_FORMAT_TYPE_POINTER:
                                bindings[i].value.value.pointer.class_id =
                                    parameters[i].target_class_id;
                                bindings[i].value.value.pointer.object_id = 1;
                                break;
                        default:
                                goto done;
                }
        }
        context.schema        = schema;
        context.schema_count  = schema_count;
        context.fields        = fields;
        context.bindings      = bindings;
        context.binding_count = parameter_count;
        valid = _evaluate_text_expression(expression, expression_length,
                                          &context, &result) &&
                result.type_code == return_type_code &&
                (result.type_code != EXECUTOR_FORMAT_TYPE_POINTER ||
                 result.value.pointer.class_id == return_target_class_id);
done:
        if (values != NULL) {
                for (i = 0; i < schema_count; i++) {
                        free(values[i]);
                }
        }
        free(values);
        free(fields);
        free(bindings);
        _free_schema_fields(schema, schema_count);
        return valid;
}

enum EXECUTOR_RESULT executor_functions_execute(
    struct database* db, const struct analyzer_statement* statement) {
        switch (statement->kind) {
                case ANALYZER_CREATE_FUNCTION:
                        return _create_function(db, statement);
                case ANALYZER_DROP_FUNCTION:
                        return _drop_function(db, statement);
                case ANALYZER_CALL:
                        return _call_function(db, statement);
                default:
                        return EXECUTOR_ERROR_NOT_IMPLEMENTED;
        }
}
