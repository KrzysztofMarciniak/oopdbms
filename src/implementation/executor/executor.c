/**
 * @file executor.c
 * @brief Dispatch checked statements to README-format executor operations.
 */

#include "interface/executor/executor.h"

#include <errno.h>
#include <float.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "implementation/executor/helper/binary_encoding.h"
#include "implementation/executor/helper/create_class_definition.h"
#include "implementation/executor/helper/create_field_descriptor.h"
#include "implementation/executor/helper/create_function_definition.h"
#include "implementation/executor/helper/create_header.h"
#include "implementation/executor/helper/create_interface_definition.h"
#include "implementation/executor/helper/create_object_record.h"
#include "implementation/executor/helper/create_table_entries.h"
#include "implementation/executor/helper/create_value_bytes.h"
#include "implementation/executor/helper/format_file.h"

static bool _name_is(const uint8_t* definition, size_t length,
                     size_t name_header_offset, size_t name_offset,
                     const char* name) {
        uint16_t name_length;

        if (definition == NULL || name == NULL || name_header_offset > length ||
            length - name_header_offset < 2 || name_offset > length) {
                return false;
        }
        name_length = executor_helper_read_u16(definition + name_header_offset);
        return name_length == strlen(name) &&
               name_length <= length - name_offset &&
               memcmp(definition + name_offset, name, name_length) == 0;
}

static bool _read_definition(const struct executor_format_file* file,
                             uint64_t offset, uint32_t length,
                             uint8_t** definition) {
        uint8_t* buffer;

        if (file == NULL || definition == NULL || length == 0 ||
            !executor_helper_range_valid(file->size, offset, length)) {
                return false;
        }
        buffer = malloc(length);
        if (buffer == NULL) {
                return false;
        }
        if (!executor_format_file_read(file, offset, buffer, length)) {
                free(buffer);
                return false;
        }
        *definition = buffer;
        return true;
}

static bool _find_class(struct executor_format_file* file, const char* name,
                        struct executor_format_class_entry* found) {
        size_t index;

        for (index = 0; index < file->header.class_count; index++) {
                struct executor_format_class_entry entry;
                uint8_t* definition;
                bool matches;

                if (!executor_format_file_read_class_entry(file, index,
                                                           &entry) ||
                    !_read_definition(file, entry.definition_offset,
                                      entry.definition_length, &definition)) {
                        return false;
                }
                matches =
                    _name_is(definition, entry.definition_length, 0, 16, name);
                free(definition);
                if (matches) {
                        if (found != NULL) {
                                *found = entry;
                        }
                        return true;
                }
        }
        return false;
}

static bool _find_interface(struct executor_format_file* file, const char* name,
                            struct executor_format_interface_entry* found) {
        size_t index;

        for (index = 0; index < file->header.interface_count; index++) {
                struct executor_format_interface_entry entry;
                uint8_t* definition;
                bool matches;

                if (!executor_format_file_read_interface_entry(file, index,
                                                               &entry) ||
                    !_read_definition(file, entry.definition_offset,
                                      entry.definition_length, &definition)) {
                        return false;
                }
                matches =
                    _name_is(definition, entry.definition_length, 0, 4, name);
                free(definition);
                if (matches) {
                        if (found != NULL) {
                                *found = entry;
                        }
                        return true;
                }
        }
        return false;
}

static uint8_t _format_type(const struct analyzer_type* type) {
        if (type == NULL) {
                return 0;
        }
        switch (type->kind) {
                case ANALYZER_TYPE_INTEGER:
                        return EXECUTOR_FORMAT_TYPE_INTEGER;
                case ANALYZER_TYPE_FLOAT:
                        return EXECUTOR_FORMAT_TYPE_FLOAT;
                case ANALYZER_TYPE_TEXT:
                        return EXECUTOR_FORMAT_TYPE_TEXT;
                case ANALYZER_TYPE_BOOL:
                        return EXECUTOR_FORMAT_TYPE_BOOL;
                case ANALYZER_TYPE_POINTER:
                        return EXECUTOR_FORMAT_TYPE_POINTER;
                default:
                        return 0;
        }
}

static bool _collect_fields(struct executor_format_file* file,
                            const struct analyzer_fields* input,
                            struct executor_format_field** fields,
                            uint32_t* max_field_id) {
        struct executor_format_field* output;
        size_t i;

        if (input->count > UINT32_MAX ||
            input->count > SIZE_MAX / sizeof *output) {
                return false;
        }
        output = calloc(input->count == 0 ? 1 : input->count, sizeof *output);
        if (output == NULL) {
                return false;
        }
        for (i = 0; i < input->count; i++) {
                const struct analyzer_field* source  = &input->items[i];
                struct executor_format_field* target = &output[i];
                struct executor_format_class_entry target_class;
                size_t prior;

                if (source->name == NULL || source->name[0] == '\0' ||
                    strlen(source->name) > UINT16_MAX ||
                    _format_type(&source->type) == 0) {
                        free(output);
                        return false;
                }
                for (prior = 0; prior < i; prior++) {
                        if (strcmp(source->name, input->items[prior].name) ==
                            0) {
                                free(output);
                                return false;
                        }
                }
                target->field_id = ++(*max_field_id);
                if (target->field_id == 0) {
                        free(output);
                        return false;
                }
                target->type_code   = _format_type(&source->type);
                target->name        = (const uint8_t*)source->name;
                target->name_length = (uint16_t)strlen(source->name);
                if (target->type_code == EXECUTOR_FORMAT_TYPE_POINTER) {
                        if (source->type.class_name == NULL ||
                            !_find_class(file, source->type.class_name,
                                         &target_class)) {
                                free(output);
                                return false;
                        }
                        target->target_class_id = target_class.class_id;
                }
        }
        *fields = output;
        return true;
}

static bool _next_id(uint32_t max_id, uint32_t* next_id) {
        if (max_id == UINT32_MAX) {
                return false;
        }
        *next_id = max_id + 1;
        if (*next_id == 0) {
                *next_id = 1;
        }
        return true;
}

static bool _max_class_id(struct executor_format_file* file, uint32_t* max_id) {
        size_t i;
        *max_id = 0;
        for (i = 0; i < file->header.class_count; i++) {
                struct executor_format_class_entry entry;
                if (!executor_format_file_read_class_entry(file, i, &entry)) {
                        return false;
                }
                if (entry.class_id > *max_id) {
                        *max_id = entry.class_id;
                }
        }
        return true;
}

static bool _max_interface_id(struct executor_format_file* file,
                              uint32_t* max_id) {
        size_t i;
        *max_id = 0;
        for (i = 0; i < file->header.interface_count; i++) {
                struct executor_format_interface_entry entry;
                if (!executor_format_file_read_interface_entry(file, i,
                                                               &entry)) {
                        return false;
                }
                if (entry.interface_id > *max_id) {
                        *max_id = entry.interface_id;
                }
        }
        return true;
}

static bool _max_field_id(struct executor_format_file* file,
                          uint32_t* max_field_id) {
        size_t i;

        *max_field_id = 0;
        for (i = 0; i < file->header.interface_count; i++) {
                struct executor_format_interface_entry entry;
                uint8_t* definition;
                uint16_t name_length;
                uint16_t field_count;
                size_t offset;
                size_t j;

                if (!executor_format_file_read_interface_entry(file, i,
                                                               &entry) ||
                    !_read_definition(file, entry.definition_offset,
                                      entry.definition_length, &definition)) {
                        return false;
                }
                if (entry.definition_length < 4) {
                        free(definition);
                        return false;
                }
                name_length = executor_helper_read_u16(definition);
                field_count = executor_helper_read_u16(definition + 2);
                offset      = 4u + name_length;
                for (j = 0; j < field_count; j++) {
                        struct executor_format_field field;
                        if (!executor_helper_read_field_descriptor(
                                definition, entry.definition_length, &offset,
                                &field)) {
                                free(definition);
                                return false;
                        }
                        if (field.field_id > *max_field_id) {
                                *max_field_id = field.field_id;
                        }
                }
                free(definition);
        }
        for (i = 0; i < file->header.class_count; i++) {
                struct executor_format_class_entry entry;
                uint8_t* definition;
                uint16_t name_length;
                uint32_t field_count;
                size_t offset;
                size_t j;

                if (!executor_format_file_read_class_entry(file, i, &entry) ||
                    !_read_definition(file, entry.definition_offset,
                                      entry.definition_length, &definition)) {
                        return false;
                }
                if (entry.definition_length < 16) {
                        free(definition);
                        return false;
                }
                name_length = executor_helper_read_u16(definition);
                field_count = executor_helper_read_u32(definition + 8);
                offset      = 16u + name_length;
                for (j = 0; j < field_count; j++) {
                        struct executor_format_field field;
                        if (!executor_helper_read_field_descriptor(
                                definition, entry.definition_length, &offset,
                                &field)) {
                                free(definition);
                                return false;
                        }
                        if (field.field_id > *max_field_id) {
                                *max_field_id = field.field_id;
                        }
                }
                free(definition);
        }
        return true;
}

static bool _append_new_table(struct executor_format_file* file,
                              const struct executor_format_section* old_table,
                              size_t old_count, size_t entry_size,
                              const uint8_t* new_entry, uint64_t* new_offset) {
        size_t old_length;
        size_t new_length;
        uint8_t* table;
        bool result;

        if (old_count > SIZE_MAX / entry_size) {
                return false;
        }
        old_length = old_count * entry_size;
        if (old_length > SIZE_MAX - entry_size) {
                return false;
        }
        new_length = old_length + entry_size;
        table      = malloc(new_length);
        if (table == NULL) {
                return false;
        }
        if (old_length != 0 &&
            !executor_format_file_read(file, old_table->offset, table,
                                       old_length)) {
                free(table);
                return false;
        }
        memcpy(table + old_length, new_entry, entry_size);
        result =
            executor_format_file_append(file, table, new_length, new_offset);
        free(table);
        return result;
}

static bool _append_table_bytes(struct executor_format_file* file,
                                const uint8_t* entries, size_t entry_size,
                                size_t count, uint64_t* new_offset) {
        size_t table_length;

        if (count > SIZE_MAX / entry_size || (count != 0 && entries == NULL) ||
            new_offset == NULL) {
                return false;
        }
        table_length = count * entry_size;
        if (table_length == 0) {
                *new_offset = 0;
                return true;
        }
        return executor_format_file_append(file, entries, table_length,
                                           new_offset);
}

struct expression_text {
        char* bytes;
        size_t length;
        size_t capacity;
};

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

static bool _find_function(struct executor_format_file* file,
                           uint32_t owner_class_id, const char* function_name,
                           size_t* found_index,
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

static bool _validate_function_expression(
    struct executor_format_file* file, uint32_t owner_class_id,
    const struct executor_format_parameter* parameters, size_t parameter_count,
    const uint8_t* expression, size_t expression_length,
    uint8_t return_type_code, uint32_t return_target_class_id);

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

struct executor_class_view {
        uint8_t* bytes;
        const uint8_t* name;
        uint16_t name_length;
        uint16_t flags;
        uint32_t parent_class_id;
        uint32_t field_count;
        uint32_t interface_count;
        struct executor_format_field* fields;
        uint32_t* interface_ids;
};

struct executor_interface_view {
        uint8_t* bytes;
        uint16_t name_length;
        uint16_t field_count;
        struct executor_format_field* fields;
};

static void _class_view_free(struct executor_class_view* view) {
        free(view->fields);
        free(view->interface_ids);
        free(view->bytes);
        memset(view, 0, sizeof *view);
}

static void _interface_view_free(struct executor_interface_view* view) {
        free(view->fields);
        free(view->bytes);
        memset(view, 0, sizeof *view);
}

static bool _load_class_view(struct executor_format_file* file,
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

static bool _load_interface_view(
    struct executor_format_file* file,
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

static bool _field_name_equal(const struct executor_format_field* field,
                              const char* name) {
        return field != NULL && name != NULL &&
               field->name_length == strlen(name) &&
               memcmp(field->name, name, field->name_length) == 0;
}

static bool _field_types_equal(const struct executor_format_field* left,
                               const struct executor_format_field* right) {
        return left->type_code == right->type_code &&
               (left->type_code != EXECUTOR_FORMAT_TYPE_POINTER ||
                left->target_class_id == right->target_class_id);
}

static bool _class_entry_by_id(struct executor_format_file* file,
                               uint32_t class_id,
                               struct executor_format_class_entry* entry);

static bool _class_field_exists(struct executor_format_file* file,
                                uint32_t class_id, const char* name,
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

static bool _class_field_by_bytes(struct executor_format_file* file,
                                  uint32_t class_id, const uint8_t* name,
                                  uint16_t name_length,
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

static bool _interface_fields_satisfied(
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

static bool _class_or_descendant_has_objects(struct executor_format_file* file,
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

static bool _descendant_defines_field(struct executor_format_file* file,
                                      uint32_t class_id, const char* name) {
        size_t i;

        for (i = 0; i < file->header.class_count; i++) {
                struct executor_format_class_entry candidate;
                uint32_t cursor;
                size_t depth = 0;

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
                        size_t field_index;
                        bool is_target = cursor == class_id;

                        if (!_class_entry_by_id(file, cursor, &current) ||
                            !_load_class_view(file, &current, &view)) {
                                return true;
                        }
                        parent_id = view.parent_class_id;
                        if (is_target) {
                                for (field_index = 0;
                                     field_index < view.field_count;
                                     field_index++) {
                                        if (_field_name_equal(
                                                &view.fields[field_index],
                                                name)) {
                                                _class_view_free(&view);
                                                return true;
                                        }
                                }
                        }
                        _class_view_free(&view);
                        if (is_target) {
                                break;
                        }
                        cursor = parent_id;
                }
                if (cursor != 0 &&
                    depth > (size_t)file->header.class_count + 1) {
                        return true;
                }
        }
        return false;
}

static void _free_schema_fields(struct executor_format_field* fields,
                                size_t count) {
        size_t i;
        for (i = 0; i < count; i++) {
                free((void*)fields[i].name);
        }
        free(fields);
}

static bool _collect_schema_fields(struct executor_format_file* file,
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

static bool _class_entry_by_id(struct executor_format_file* file,
                               uint32_t class_id,
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

static bool _interface_entry_by_id(
    struct executor_format_file* file, uint32_t interface_id,
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

static bool _rewrite_class_entry(
    struct executor_format_file* file, size_t index,
    const struct executor_format_class_entry* entry, const uint8_t* definition,
    size_t definition_length) {
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

static bool _class_index_by_name(struct executor_format_file* file,
                                 const char* name, size_t* index,
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

static bool _interface_index_by_name(
    struct executor_format_file* file, const char* name, size_t* index,
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

static bool _store_class_view(struct executor_format_file* file, size_t index,
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

static bool _remove_class_table_entry(struct executor_format_file* file,
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

static bool _remove_interface_table_entry(struct executor_format_file* file,
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

static bool _class_has_external_references(
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

struct executor_value {
        uint8_t type_code;

        union {
                int64_t integer;
                double floating;
                bool boolean;

                struct {
                        uint32_t class_id;
                        uint64_t object_id;
                } pointer;

                struct {
                        const uint8_t* bytes;
                        size_t length;
                } text;
        } value;
};

static bool _parse_i64(const char* text, int64_t* value) {
        char* end;
        long long parsed;

        if (text == NULL || text[0] == '\0') {
                return false;
        }
        errno  = 0;
        parsed = strtoll(text, &end, 10);
        if (errno == ERANGE || *end != '\0') {
                return false;
        }
        *value = (int64_t)parsed;
        return true;
}

static bool _parse_u64(const char* text, uint64_t* value) {
        char* end;
        unsigned long long parsed;

        if (text == NULL || text[0] == '\0' || text[0] == '-') {
                return false;
        }
        errno  = 0;
        parsed = strtoull(text, &end, 10);
        if (errno == ERANGE || *end != '\0') {
                return false;
        }
        *value = (uint64_t)parsed;
        return true;
}

static bool _parse_double(const char* text, double* value) {
        char* end;
        double parsed;

        if (text == NULL || text[0] == '\0') {
                return false;
        }
        errno  = 0;
        parsed = strtod(text, &end);
        if (errno == ERANGE || *end != '\0') {
                return false;
        }
        *value = parsed;
        return true;
}

static bool _find_object_in_class(
    struct executor_format_file* file,
    const struct executor_format_class_entry* entry, uint64_t object_id,
    uint8_t** segment_bytes, size_t* record_offset,
    struct executor_format_object_header* header) {
        uint8_t* segment = NULL;
        size_t length;
        size_t offset = 0;

        if (entry->data_length > SIZE_MAX ||
            !executor_helper_range_valid(file->size, entry->data_offset,
                                         entry->data_length)) {
                return false;
        }
        length = (size_t)entry->data_length;
        if (length != 0) {
                segment = malloc(length);
                if (segment == NULL ||
                    !executor_format_file_read(file, entry->data_offset,
                                               segment, length)) {
                        free(segment);
                        return false;
                }
        }
        while (offset < length) {
                struct executor_format_object_header current;
                if (!executor_helper_read_object_header(
                        segment + offset, length - offset, &current) ||
                    current.record_length > length - offset) {
                        free(segment);
                        return false;
                }
                if (current.object_id == object_id) {
                        if (segment_bytes != NULL) {
                                *segment_bytes = segment;
                        } else {
                                free(segment);
                        }
                        if (record_offset != NULL) {
                                *record_offset = offset;
                        }
                        if (header != NULL) {
                                *header = current;
                        }
                        return true;
                }
                offset += current.record_length;
        }
        free(segment);
        return false;
}

static bool _object_exists(struct executor_format_file* file, uint32_t class_id,
                           uint64_t object_id) {
        struct executor_format_class_entry entry;
        bool exists;

        if (!_class_entry_by_id(file, class_id, &entry)) {
                return false;
        }
        exists =
            _find_object_in_class(file, &entry, object_id, NULL, NULL, NULL);
        return exists;
}

static bool _encode_input_value(struct executor_format_file* file,
                                const struct executor_format_field* field,
                                const struct analyzer_value* input,
                                uint8_t** output, uint32_t* output_length) {
        uint8_t bytes[12];
        size_t written = 0;
        int64_t integer;
        double floating;
        uint64_t object_id;
        struct executor_format_class_entry target;

        if (field == NULL || input == NULL || output == NULL ||
            output_length == NULL) {
                return false;
        }
        switch (field->type_code) {
                case EXECUTOR_FORMAT_TYPE_INTEGER:
                        if (input->kind != ANALYZER_VALUE_INTEGER ||
                            !_parse_i64(input->text, &integer) ||
                            !executor_helper_create_integer_value(
                                bytes, sizeof bytes, integer, &written)) {
                                return false;
                        }
                        break;
                case EXECUTOR_FORMAT_TYPE_FLOAT:
                        if (input->kind == ANALYZER_VALUE_FLOAT) {
                                if (!_parse_double(input->text, &floating)) {
                                        return false;
                                }
                        } else if (input->kind == ANALYZER_VALUE_INTEGER) {
                                if (!_parse_i64(input->text, &integer)) {
                                        return false;
                                }
                                floating = (double)integer;
                        } else {
                                return false;
                        }
                        if (!executor_helper_create_float_value(
                                bytes, sizeof bytes, floating, &written)) {
                                return false;
                        }
                        break;
                case EXECUTOR_FORMAT_TYPE_TEXT:
                        if (input->kind != ANALYZER_VALUE_TEXT ||
                            input->text == NULL ||
                            strlen(input->text) > UINT32_MAX) {
                                return false;
                        }
                        written = strlen(input->text);
                        *output = malloc(written == 0 ? 1 : written);
                        if (*output == NULL ||
                            !executor_helper_create_text_value(
                                *output, written, (const uint8_t*)input->text,
                                written, &written)) {
                                free(*output);
                                *output = NULL;
                                return false;
                        }
                        *output_length = (uint32_t)written;
                        return true;
                case EXECUTOR_FORMAT_TYPE_BOOL:
                        if (input->kind != ANALYZER_VALUE_BOOL ||
                            input->text == NULL ||
                            (strcmp(input->text, "true") != 0 &&
                             strcmp(input->text, "false") != 0) ||
                            !executor_helper_create_bool_value(
                                bytes, sizeof bytes,
                                strcmp(input->text, "true") == 0, &written)) {
                                return false;
                        }
                        break;
                case EXECUTOR_FORMAT_TYPE_POINTER:
                        if (input->kind != ANALYZER_VALUE_POINTER ||
                            input->class_name == NULL ||
                            !_find_class(file, input->class_name, &target) ||
                            target.class_id != field->target_class_id ||
                            !_parse_u64(input->text, &object_id) ||
                            !_object_exists(file, target.class_id, object_id) ||
                            !executor_helper_create_pointer_value(
                                bytes, sizeof bytes, target.class_id, object_id,
                                &written)) {
                                return false;
                        }
                        break;
                default:
                        return false;
        }
        *output = malloc(written == 0 ? 1 : written);
        if (*output == NULL) {
                return false;
        }
        memcpy(*output, bytes, written);
        *output_length = (uint32_t)written;
        return true;
}

static bool _schema_field_index(const struct executor_format_field* fields,
                                size_t count, const char* name, size_t* index) {
        size_t i;

        for (i = 0; i < count; i++) {
                if (_field_name_equal(&fields[i], name)) {
                        *index = i;
                        return true;
                }
        }
        return false;
}

static bool _rewrite_object_segment(struct executor_format_file* file,
                                    size_t class_index,
                                    struct executor_format_class_entry* entry,
                                    const uint8_t* segment,
                                    size_t segment_length) {
        uint8_t* definition  = NULL;
        uint64_t data_offset = 0;
        bool result;

        if (segment_length > UINT64_MAX ||
            !_read_definition(file, entry->definition_offset,
                              entry->definition_length, &definition)) {
                return false;
        }
        if (segment_length != 0 &&
            !executor_format_file_append(file, segment, segment_length,
                                         &data_offset)) {
                free(definition);
                return false;
        }
        entry->data_offset = data_offset;
        entry->data_length = segment_length;
        result = _rewrite_class_entry(file, class_index, entry, definition,
                                      entry->definition_length);
        free(definition);
        return result;
}

static enum EXECUTOR_RESULT _new_object(
    struct database* db, const struct analyzer_statement* statement) {
        struct executor_format_file file;
        struct executor_format_class_entry entry;
        struct executor_format_field* schema               = NULL;
        struct executor_format_object_field* record_fields = NULL;
        struct executor_format_object_record record;
        uint8_t* record_bytes = NULL;
        uint8_t* old_segment  = NULL;
        uint8_t* segment      = NULL;
        size_t schema_count   = 0;
        size_t class_index;
        size_t record_length;
        size_t segment_length;
        size_t old_segment_length;
        size_t i;
        uint64_t object_id;

        enum EXECUTOR_RESULT result = EXECUTOR_ERROR_BAD_STATEMENT;

        if (statement->name == NULL || !db->format_mode ||
            !executor_format_file_load(db, &file) ||
            !_class_index_by_name(&file, statement->name, &class_index,
                                  &entry) ||
            !_collect_schema_fields(&file, entry.class_id, &schema,
                                    &schema_count) ||
            schema_count == 0 || schema_count > UINT16_MAX ||
            statement->assignments.items == NULL ||
            statement->assignments.count != schema_count ||
            entry.next_object_id == 0 || entry.next_object_id == UINT64_MAX) {
                goto done;
        }
        object_id     = entry.next_object_id;
        record_fields = calloc(schema_count, sizeof *record_fields);
        if (record_fields == NULL) {
                result = EXECUTOR_ERROR_STORAGE_FAILED;
                goto done;
        }
        for (i = 0; i < statement->assignments.count; i++) {
                const struct analyzer_assignment* assignment =
                    &statement->assignments.items[i];
                size_t field_index;
                size_t previous;
                uint8_t* value_bytes = NULL;
                uint32_t value_length;

                if (assignment->field == NULL ||
                    !_schema_field_index(schema, schema_count,
                                         assignment->field, &field_index)) {
                        goto done;
                }
                for (previous = 0; previous < i; previous++) {
                        if (strcmp(statement->assignments.items[previous].field,
                                   assignment->field) == 0) {
                                goto done;
                        }
                }
                if (!_encode_input_value(&file, &schema[field_index],
                                         &assignment->value, &value_bytes,
                                         &value_length)) {
                        free(value_bytes);
                        goto done;
                }
                record_fields[field_index].field_id =
                    schema[field_index].field_id;
                record_fields[field_index].type_code =
                    schema[field_index].type_code;
                record_fields[field_index].value        = value_bytes;
                record_fields[field_index].value_length = value_length;
        }
        for (i = 0; i < schema_count; i++) {
                if (record_fields[i].value == NULL) {
                        goto done;
                }
        }
        record.object_id   = object_id;
        record.flags       = 0;
        record.fields      = record_fields;
        record.field_count = (uint16_t)schema_count;
        record_length      = 16;
        for (i = 0; i < schema_count; i++) {
                size_t add = 9 + record_fields[i].value_length;
                if (add > SIZE_MAX - record_length) {
                        goto done;
                }
                record_length += add;
        }
        record_bytes = malloc(record_length);
        if (record_bytes == NULL ||
            !executor_helper_create_object_record(record_bytes, record_length,
                                                  &record, &record_length)) {
                result = EXECUTOR_ERROR_STORAGE_FAILED;
                goto done;
        }
        if (entry.data_length > SIZE_MAX ||
            !executor_helper_range_valid(file.size, entry.data_offset,
                                         entry.data_length)) {
                result = EXECUTOR_ERROR_STORAGE_FAILED;
                goto done;
        }
        old_segment_length = (size_t)entry.data_length;
        if (old_segment_length != 0) {
                old_segment = malloc(old_segment_length);
                if (old_segment == NULL ||
                    !executor_format_file_read(&file, entry.data_offset,
                                               old_segment,
                                               old_segment_length)) {
                        result = EXECUTOR_ERROR_STORAGE_FAILED;
                        goto done;
                }
        }
        if (record_length > SIZE_MAX - old_segment_length) {
                result = EXECUTOR_ERROR_STORAGE_FAILED;
                goto done;
        }
        segment_length = old_segment_length + record_length;
        segment        = malloc(segment_length);
        if (segment == NULL) {
                result = EXECUTOR_ERROR_STORAGE_FAILED;
                goto done;
        }
        if (old_segment_length != 0) {
                memcpy(segment, old_segment, old_segment_length);
        }
        memcpy(segment + old_segment_length, record_bytes, record_length);
        if (entry.object_count == UINT64_MAX) {
                goto done;
        }
        entry.object_count++;
        entry.next_object_id++;
        if (!_rewrite_object_segment(&file, class_index, &entry, segment,
                                     segment_length) ||
            !executor_format_file_commit(&file)) {
                result = EXECUTOR_ERROR_SAVE_FAILED;
                goto done;
        }
        printf("%s.%llu\n", statement->name, (unsigned long long)object_id);
        result = EXECUTOR_OK;
done:
        if (record_fields != NULL) {
                for (i = 0; i < schema_count; i++) {
                        free((void*)record_fields[i].value);
                }
        }
        free(record_fields);
        free(record_bytes);
        free(old_segment);
        free(segment);
        _free_schema_fields(schema, schema_count);
        return result;
}

static void _free_object_fields(struct executor_format_object_field* fields,
                                size_t count) {
        size_t i;
        for (i = 0; i < count; i++) {
                free((void*)fields[i].value);
        }
        free(fields);
}

static bool _schema_index_by_id(const struct executor_format_field* schema,
                                size_t count, uint32_t field_id,
                                size_t* index) {
        size_t i;
        for (i = 0; i < count; i++) {
                if (schema[i].field_id == field_id) {
                        *index = i;
                        return true;
                }
        }
        return false;
}

static bool _load_object_fields(struct executor_format_file* file,
                                const struct executor_format_class_entry* entry,
                                uint64_t object_id,
                                const struct executor_format_field* schema,
                                size_t schema_count, uint8_t** segment,
                                size_t* record_offset,
                                struct executor_format_object_header* header,
                                struct executor_format_object_field** fields) {
        struct executor_format_object_header object_header;
        struct executor_format_object_field* result = NULL;
        uint8_t* bytes                              = NULL;
        bool* seen                                  = NULL;
        size_t offset;
        size_t i;

        if (!_find_object_in_class(file, entry, object_id, &bytes, &offset,
                                   &object_header) ||
            object_header.field_count != schema_count) {
                free(bytes);
                return false;
        }
        result = calloc(schema_count == 0 ? 1 : schema_count, sizeof *result);
        seen   = calloc(schema_count == 0 ? 1 : schema_count, sizeof *seen);
        if (result == NULL || seen == NULL) {
                free(bytes);
                free(result);
                free(seen);
                return false;
        }
        {
                size_t cursor = EXECUTOR_FORMAT_OBJECT_HEADER_SIZE;
                size_t end    = object_header.record_length;
                for (i = 0; i < object_header.field_count; i++) {
                        struct executor_format_object_field_view view;
                        size_t schema_index;
                        uint8_t* value;
                        if (!executor_helper_read_object_field(
                                bytes + offset, object_header.record_length,
                                &cursor, &view) ||
                            !_schema_index_by_id(schema, schema_count,
                                                 view.field_id,
                                                 &schema_index) ||
                            seen[schema_index] ||
                            schema[schema_index].type_code != view.type_code) {
                                free(bytes);
                                free(seen);
                                _free_object_fields(result, schema_count);
                                return false;
                        }
                        value = malloc(
                            view.value_length == 0 ? 1 : view.value_length);
                        if (value == NULL) {
                                free(bytes);
                                free(seen);
                                _free_object_fields(result, schema_count);
                                return false;
                        }
                        memcpy(value, view.value, view.value_length);
                        result[schema_index].field_id     = view.field_id;
                        result[schema_index].type_code    = view.type_code;
                        result[schema_index].value        = value;
                        result[schema_index].value_length = view.value_length;
                        seen[schema_index]                = true;
                }
                if (cursor != end) {
                        free(bytes);
                        free(seen);
                        _free_object_fields(result, schema_count);
                        return false;
                }
        }
        for (i = 0; i < schema_count; i++) {
                if (!seen[i]) {
                        free(bytes);
                        free(seen);
                        _free_object_fields(result, schema_count);
                        return false;
                }
        }
        free(seen);
        if (segment != NULL) {
                *segment = bytes;
        } else {
                free(bytes);
        }
        if (record_offset != NULL) {
                *record_offset = offset;
        }
        if (header != NULL) {
                *header = object_header;
        }
        if (fields != NULL) {
                *fields = result;
        } else {
                _free_object_fields(result, schema_count);
        }
        return true;
}

static bool _serialize_object_fields(
    uint64_t object_id, const struct executor_format_object_field* fields,
    size_t count, uint8_t** bytes, size_t* length) {
        struct executor_format_object_record record;
        size_t capacity = EXECUTOR_FORMAT_OBJECT_HEADER_SIZE;
        size_t i;

        if (count > UINT16_MAX) {
                return false;
        }
        for (i = 0; i < count; i++) {
                size_t field_length = 9 + fields[i].value_length;
                if (field_length > SIZE_MAX - capacity) {
                        return false;
                }
                capacity += field_length;
        }
        *bytes = malloc(capacity);
        if (*bytes == NULL) {
                return false;
        }
        record.object_id   = object_id;
        record.flags       = 0;
        record.fields      = fields;
        record.field_count = (uint16_t)count;
        if (!executor_helper_create_object_record(*bytes, capacity, &record,
                                                  length)) {
                free(*bytes);
                *bytes = NULL;
                return false;
        }
        return true;
}

static enum EXECUTOR_RESULT _set_object(
    struct database* db, const struct analyzer_statement* statement) {
        struct executor_format_file file;
        struct executor_format_class_entry entry;
        struct executor_format_field* schema        = NULL;
        struct executor_format_object_field* fields = NULL;
        struct executor_format_object_header header;
        uint8_t* old_segment      = NULL;
        uint8_t* new_record_bytes = NULL;
        uint8_t* new_segment      = NULL;
        uint64_t object_id;
        size_t schema_count = 0;
        size_t class_index;
        size_t record_offset;
        size_t record_length;
        size_t new_record_length;
        size_t new_segment_length;
        size_t i;
        enum EXECUTOR_RESULT result = EXECUTOR_ERROR_BAD_STATEMENT;

        if (statement->name == NULL || statement->id == NULL ||
            statement->assignments.items == NULL ||
            statement->assignments.count == 0 ||
            !_parse_u64(statement->id, &object_id) || object_id == 0 ||
            !db->format_mode || !executor_format_file_load(db, &file) ||
            !_class_index_by_name(&file, statement->name, &class_index,
                                  &entry) ||
            !_collect_schema_fields(&file, entry.class_id, &schema,
                                    &schema_count) ||
            !_load_object_fields(&file, &entry, object_id, schema, schema_count,
                                 &old_segment, &record_offset, &header,
                                 &fields)) {
                goto done;
        }
        for (i = 0; i < statement->assignments.count; i++) {
                const struct analyzer_assignment* assignment =
                    &statement->assignments.items[i];
                size_t field_index;
                size_t previous;
                uint8_t* value = NULL;
                uint32_t value_length;

                if (assignment->field == NULL ||
                    !_schema_field_index(schema, schema_count,
                                         assignment->field, &field_index)) {
                        goto done;
                }
                for (previous = 0; previous < i; previous++) {
                        if (strcmp(statement->assignments.items[previous].field,
                                   assignment->field) == 0) {
                                goto done;
                        }
                }
                if (!_encode_input_value(&file, &schema[field_index],
                                         &assignment->value, &value,
                                         &value_length)) {
                        free(value);
                        goto done;
                }
                free((void*)fields[field_index].value);
                fields[field_index].value        = value;
                fields[field_index].value_length = value_length;
        }
        if (!_serialize_object_fields(object_id, fields, schema_count,
                                      &new_record_bytes, &new_record_length)) {
                result = EXECUTOR_ERROR_STORAGE_FAILED;
                goto done;
        }
        record_length = header.record_length;
        if (entry.data_length > SIZE_MAX ||
            record_offset > (size_t)entry.data_length ||
            record_length > (size_t)entry.data_length - record_offset ||
            new_record_length >
                SIZE_MAX - ((size_t)entry.data_length - record_length)) {
                result = EXECUTOR_ERROR_STORAGE_FAILED;
                goto done;
        }
        new_segment_length =
            (size_t)entry.data_length - record_length + new_record_length;
        new_segment = malloc(new_segment_length == 0 ? 1 : new_segment_length);
        if (new_segment == NULL) {
                result = EXECUTOR_ERROR_STORAGE_FAILED;
                goto done;
        }
        memcpy(new_segment, old_segment, record_offset);
        memcpy(new_segment + record_offset, new_record_bytes,
               new_record_length);
        memcpy(new_segment + record_offset + new_record_length,
               old_segment + record_offset + record_length,
               (size_t)entry.data_length - record_offset - record_length);
        if (!_rewrite_object_segment(&file, class_index, &entry, new_segment,
                                     new_segment_length) ||
            !executor_format_file_commit(&file)) {
                result = EXECUTOR_ERROR_SAVE_FAILED;
                goto done;
        }
        result = EXECUTOR_OK;
done:
        free(old_segment);
        free(new_record_bytes);
        free(new_segment);
        _free_object_fields(fields, schema_count);
        _free_schema_fields(schema, schema_count);
        return result;
}

static bool _object_is_referenced(struct executor_format_file* file,
                                  uint32_t target_class_id,
                                  uint64_t target_object_id) {
        size_t i;

        for (i = 0; i < file->header.class_count; i++) {
                struct executor_format_class_entry entry;
                uint8_t* segment = NULL;
                size_t length;
                size_t offset = 0;

                if (!executor_format_file_read_class_entry(file, i, &entry) ||
                    entry.data_length > SIZE_MAX ||
                    !executor_helper_range_valid(file->size, entry.data_offset,
                                                 entry.data_length)) {
                        return true;
                }
                length = (size_t)entry.data_length;
                if (length != 0) {
                        segment = malloc(length);
                        if (segment == NULL ||
                            !executor_format_file_read(file, entry.data_offset,
                                                       segment, length)) {
                                free(segment);
                                return true;
                        }
                }
                while (offset < length) {
                        struct executor_format_object_header header;
                        size_t cursor;
                        size_t field_index;

                        if (!executor_helper_read_object_header(
                                segment + offset, length - offset, &header) ||
                            header.record_length > length - offset) {
                                free(segment);
                                return true;
                        }
                        cursor = EXECUTOR_FORMAT_OBJECT_HEADER_SIZE;
                        for (field_index = 0; field_index < header.field_count;
                             field_index++) {
                                struct executor_format_object_field_view field;
                                if (!executor_helper_read_object_field(
                                        segment + offset, header.record_length,
                                        &cursor, &field)) {
                                        free(segment);
                                        return true;
                                }
                                if (field.type_code ==
                                        EXECUTOR_FORMAT_TYPE_POINTER &&
                                    executor_helper_read_u32(field.value) ==
                                        target_class_id &&
                                    executor_helper_read_u64(field.value + 4) ==
                                        target_object_id &&
                                    !(entry.class_id == target_class_id &&
                                      header.object_id == target_object_id)) {
                                        free(segment);
                                        return true;
                                }
                        }
                        offset += header.record_length;
                }
                free(segment);
        }
        return false;
}

static enum EXECUTOR_RESULT _delete_object(
    struct database* db, const struct analyzer_statement* statement) {
        struct executor_format_file file;
        struct executor_format_class_entry entry;
        struct executor_format_field* schema               = NULL;
        struct executor_format_object_field* unused_fields = NULL;
        struct executor_format_object_header header;
        uint8_t* old_segment = NULL;
        uint8_t* new_segment = NULL;
        uint64_t object_id;
        size_t schema_count = 0;
        size_t class_index;
        size_t record_offset;
        size_t new_segment_length;
        enum EXECUTOR_RESULT result = EXECUTOR_ERROR_BAD_STATEMENT;

        if (statement->name == NULL || statement->id == NULL ||
            !_parse_u64(statement->id, &object_id) || object_id == 0 ||
            !db->format_mode || !executor_format_file_load(db, &file) ||
            !_class_index_by_name(&file, statement->name, &class_index,
                                  &entry) ||
            !_collect_schema_fields(&file, entry.class_id, &schema,
                                    &schema_count) ||
            !_load_object_fields(&file, &entry, object_id, schema, schema_count,
                                 &old_segment, &record_offset, &header,
                                 &unused_fields)) {
                goto done;
        }
        if (_object_is_referenced(&file, entry.class_id, object_id) ||
            entry.object_count == 0 ||
            header.record_length > (size_t)entry.data_length - record_offset) {
                goto done;
        }
        new_segment_length = (size_t)entry.data_length - header.record_length;
        new_segment = malloc(new_segment_length == 0 ? 1 : new_segment_length);
        if (new_segment == NULL) {
                result = EXECUTOR_ERROR_STORAGE_FAILED;
                goto done;
        }
        memcpy(new_segment, old_segment, record_offset);
        memcpy(
            new_segment + record_offset,
            old_segment + record_offset + header.record_length,
            (size_t)entry.data_length - record_offset - header.record_length);
        entry.object_count--;
        if (!_rewrite_object_segment(&file, class_index, &entry, new_segment,
                                     new_segment_length) ||
            !executor_format_file_commit(&file)) {
                result = EXECUTOR_ERROR_SAVE_FAILED;
                goto done;
        }
        result = EXECUTOR_OK;
done:
        free(old_segment);
        free(new_segment);
        _free_object_fields(unused_fields, schema_count);
        _free_schema_fields(schema, schema_count);
        return result;
}

static bool _read_double_le(const uint8_t* bytes, double* value) {
        static const uint8_t one_le[8] = {0, 0, 0, 0, 0, 0, 0xf0, 0x3f};
        static const uint8_t one_be[8] = {0x3f, 0xf0, 0, 0, 0, 0, 0, 0};
        uint8_t native[8];
        size_t i;

        if (sizeof(double) != 8 || DBL_MANT_DIG != 53 || DBL_MAX_EXP != 1024 ||
            DBL_MIN_EXP != -1021) {
                return false;
        }
        {
                double one = 1.0;
                memcpy(native, &one, sizeof native);
        }
        if (memcmp(native, one_le, 8) == 0) {
                memcpy(value, bytes, 8);
                return true;
        }
        if (memcmp(native, one_be, 8) == 0) {
                for (i = 0; i < 8; i++) {
                        native[i] = bytes[7 - i];
                }
                memcpy(value, native, 8);
                return true;
        }
        return false;
}

static bool _value_from_bytes(uint8_t type_code, const uint8_t* bytes,
                              size_t length, struct executor_value* value) {
        if (value == NULL || (length != 0 && bytes == NULL)) {
                return false;
        }
        memset(value, 0, sizeof *value);
        value->type_code = type_code;
        switch (type_code) {
                case EXECUTOR_FORMAT_TYPE_INTEGER: {
                        uint64_t raw;
                        if (length != 8) {
                                return false;
                        }
                        raw = executor_helper_read_u64(bytes);
                        if (raw <= INT64_MAX) {
                                value->value.integer = (int64_t)raw;
                        } else {
                                value->value.integer =
                                    -1 - (int64_t)(UINT64_MAX - raw);
                        }
                        return true;
                }
                case EXECUTOR_FORMAT_TYPE_FLOAT:
                        return length == 8 &&
                               _read_double_le(bytes, &value->value.floating);
                case EXECUTOR_FORMAT_TYPE_TEXT:
                        value->value.text.bytes  = bytes;
                        value->value.text.length = length;
                        return true;
                case EXECUTOR_FORMAT_TYPE_BOOL:
                        if (length != 1 || bytes[0] > 1) {
                                return false;
                        }
                        value->value.boolean = bytes[0] != 0;
                        return true;
                case EXECUTOR_FORMAT_TYPE_POINTER:
                        if (length != 12) {
                                return false;
                        }
                        value->value.pointer.class_id =
                            executor_helper_read_u32(bytes);
                        value->value.pointer.object_id =
                            executor_helper_read_u64(bytes + 4);
                        return true;
                default:
                        return false;
        }
}

static bool _value_from_analyzer(const struct analyzer_value* input,
                                 struct executor_value* value) {
        int64_t integer;
        double floating;

        if (input == NULL || value == NULL) {
                return false;
        }
        memset(value, 0, sizeof *value);
        switch (input->kind) {
                case ANALYZER_VALUE_INTEGER:
                        if (!_parse_i64(input->text, &integer)) {
                                return false;
                        }
                        value->type_code     = EXECUTOR_FORMAT_TYPE_INTEGER;
                        value->value.integer = integer;
                        return true;
                case ANALYZER_VALUE_FLOAT:
                        if (!_parse_double(input->text, &floating)) {
                                return false;
                        }
                        value->type_code      = EXECUTOR_FORMAT_TYPE_FLOAT;
                        value->value.floating = floating;
                        return true;
                case ANALYZER_VALUE_TEXT:
                        value->type_code         = EXECUTOR_FORMAT_TYPE_TEXT;
                        value->value.text.bytes  = (const uint8_t*)input->text;
                        value->value.text.length = strlen(input->text);
                        return true;
                case ANALYZER_VALUE_BOOL:
                        if (input->text == NULL ||
                            (strcmp(input->text, "true") != 0 &&
                             strcmp(input->text, "false") != 0)) {
                                return false;
                        }
                        value->type_code     = EXECUTOR_FORMAT_TYPE_BOOL;
                        value->value.boolean = strcmp(input->text, "true") == 0;
                        return true;
                default:
                        return false;
        }
}

static bool _numeric_value(const struct executor_value* value, double* number,
                           bool* is_integer) {
        if (value->type_code == EXECUTOR_FORMAT_TYPE_INTEGER) {
                *number     = (double)value->value.integer;
                *is_integer = true;
                return true;
        }
        if (value->type_code == EXECUTOR_FORMAT_TYPE_FLOAT) {
                *number     = value->value.floating;
                *is_integer = false;
                return true;
        }
        return false;
}

static bool _multiply_i64(int64_t left, int64_t right, int64_t* output) {
        if (left > 0) {
                if ((right > 0 && left > INT64_MAX / right) ||
                    (right < 0 && right < INT64_MIN / left)) {
                        return false;
                }
        } else if (left < 0) {
                if ((right > 0 && left < INT64_MIN / right) ||
                    (right < 0 && right < INT64_MAX / left)) {
                        return false;
                }
        }
        *output = left * right;
        return true;
}

static bool _evaluate_ast(const struct analyzer_expression* expression,
                          const struct executor_format_field* schema,
                          size_t schema_count,
                          const struct executor_format_object_field* fields,
                          struct executor_value* result) {
        struct executor_value left;
        struct executor_value right;
        size_t index;

        if (expression == NULL || result == NULL) {
                return false;
        }
        switch (expression->kind) {
                case ANALYZER_EXPRESSION_LITERAL:
                        return _value_from_analyzer(&expression->value, result);
                case ANALYZER_EXPRESSION_FIELD:
                        if (expression->field == NULL ||
                            !_schema_field_index(schema, schema_count,
                                                 expression->field, &index)) {
                                return false;
                        }
                        return _value_from_bytes(
                            fields[index].type_code, fields[index].value,
                            fields[index].value_length, result);
                case ANALYZER_EXPRESSION_NEGATE:
                        if (!_evaluate_ast(expression->left, schema,
                                           schema_count, fields, &left)) {
                                return false;
                        }
                        if (left.type_code == EXECUTOR_FORMAT_TYPE_INTEGER) {
                                if (left.value.integer == INT64_MIN) {
                                        return false;
                                }
                                result->type_code =
                                    EXECUTOR_FORMAT_TYPE_INTEGER;
                                result->value.integer = -left.value.integer;
                                return true;
                        }
                        if (left.type_code == EXECUTOR_FORMAT_TYPE_FLOAT) {
                                result->type_code = EXECUTOR_FORMAT_TYPE_FLOAT;
                                result->value.floating = -left.value.floating;
                                return true;
                        }
                        return false;
                case ANALYZER_EXPRESSION_BINARY:
                        if (!_evaluate_ast(expression->left, schema,
                                           schema_count, fields, &left) ||
                            !_evaluate_ast(expression->right, schema,
                                           schema_count, fields, &right)) {
                                return false;
                        }
                        break;
                default:
                        return false;
        }
        if (expression->op <= ANALYZER_OPERATOR_DIVIDE) {
                double left_number;
                double right_number;
                bool left_integer;
                bool right_integer;
                if (!_numeric_value(&left, &left_number, &left_integer) ||
                    !_numeric_value(&right, &right_number, &right_integer)) {
                        return false;
                }
                if (left_integer && right_integer &&
                    expression->op != ANALYZER_OPERATOR_DIVIDE) {
                        int64_t a = left.value.integer;
                        int64_t b = right.value.integer;
                        int64_t out;
                        if (expression->op == ANALYZER_OPERATOR_ADD) {
                                if ((b > 0 && a > INT64_MAX - b) ||
                                    (b < 0 && a < INT64_MIN - b)) {
                                        return false;
                                }
                                out = a + b;
                        } else if (expression->op ==
                                   ANALYZER_OPERATOR_SUBTRACT) {
                                if ((b < 0 && a > INT64_MAX + b) ||
                                    (b > 0 && a < INT64_MIN + b)) {
                                        return false;
                                }
                                out = a - b;
                        } else {
                                if (!_multiply_i64(a, b, &out)) {
                                        return false;
                                }
                        }
                        result->type_code     = EXECUTOR_FORMAT_TYPE_INTEGER;
                        result->value.integer = out;
                        return true;
                }
                if (expression->op == ANALYZER_OPERATOR_DIVIDE &&
                    right_number == 0.0) {
                        return false;
                }
                result->type_code = EXECUTOR_FORMAT_TYPE_FLOAT;
                switch (expression->op) {
                        case ANALYZER_OPERATOR_ADD:
                                result->value.floating =
                                    left_number + right_number;
                                break;
                        case ANALYZER_OPERATOR_SUBTRACT:
                                result->value.floating =
                                    left_number - right_number;
                                break;
                        case ANALYZER_OPERATOR_MULTIPLY:
                                result->value.floating =
                                    left_number * right_number;
                                break;
                        case ANALYZER_OPERATOR_DIVIDE:
                                result->value.floating =
                                    left_number / right_number;
                                break;
                        default:
                                return false;
                }
                return true;
        }
        {
                int comparison;
                double a;
                double b;
                bool ai;
                bool bi;
                if (left.type_code == EXECUTOR_FORMAT_TYPE_INTEGER &&
                    right.type_code == EXECUTOR_FORMAT_TYPE_INTEGER) {
                        comparison =
                            left.value.integer < right.value.integer   ? -1
                            : left.value.integer > right.value.integer ? 1
                                                                       : 0;
                } else if (_numeric_value(&left, &a, &ai) &&
                           _numeric_value(&right, &b, &bi)) {
                        comparison = a < b ? -1 : a > b ? 1 : 0;
                } else if (left.type_code != right.type_code) {
                        return false;
                } else if (left.type_code == EXECUTOR_FORMAT_TYPE_TEXT) {
                        size_t common =
                            left.value.text.length < right.value.text.length
                                ? left.value.text.length
                                : right.value.text.length;
                        comparison = memcmp(left.value.text.bytes,
                                            right.value.text.bytes, common);
                        if (comparison == 0) {
                                comparison = left.value.text.length <
                                                     right.value.text.length
                                                 ? -1
                                             : left.value.text.length >
                                                     right.value.text.length
                                                 ? 1
                                                 : 0;
                        }
                } else if (left.type_code == EXECUTOR_FORMAT_TYPE_BOOL) {
                        comparison =
                            (int)left.value.boolean - (int)right.value.boolean;
                } else if (left.type_code == EXECUTOR_FORMAT_TYPE_POINTER) {
                        if (left.value.pointer.class_id !=
                            right.value.pointer.class_id) {
                                comparison =
                                    left.value.pointer.class_id <
                                            right.value.pointer.class_id
                                        ? -1
                                        : 1;
                        } else {
                                comparison =
                                    left.value.pointer.object_id <
                                            right.value.pointer.object_id
                                        ? -1
                                    : left.value.pointer.object_id >
                                            right.value.pointer.object_id
                                        ? 1
                                        : 0;
                        }
                } else {
                        return false;
                }
                result->type_code = EXECUTOR_FORMAT_TYPE_BOOL;
                switch (expression->op) {
                        case ANALYZER_OPERATOR_EQUAL:
                                result->value.boolean = comparison == 0;
                                break;
                        case ANALYZER_OPERATOR_NOT_EQUAL:
                                result->value.boolean = comparison != 0;
                                break;
                        case ANALYZER_OPERATOR_LESS:
                                result->value.boolean = comparison < 0;
                                break;
                        case ANALYZER_OPERATOR_LESS_EQUAL:
                                result->value.boolean = comparison <= 0;
                                break;
                        case ANALYZER_OPERATOR_GREATER:
                                result->value.boolean = comparison > 0;
                                break;
                        case ANALYZER_OPERATOR_GREATER_EQUAL:
                                result->value.boolean = comparison >= 0;
                                break;
                        default:
                                return false;
                }
                return true;
        }
}

struct executor_binding {
        const uint8_t* name;
        size_t name_length;
        struct executor_value value;
};

struct executor_eval_context {
        const struct executor_format_field* schema;
        size_t schema_count;
        const struct executor_format_object_field* fields;
        const struct executor_binding* bindings;
        size_t binding_count;
};

struct executor_text_parser {
        const uint8_t* bytes;
        size_t length;
        size_t position;
        struct executor_eval_context* context;
        char** allocations;
        size_t allocation_count;
};

struct executor_parameter_view {
        const uint8_t* name;
        uint16_t name_length;
        uint8_t type_code;
        uint32_t target_class_id;
};

struct executor_function_view {
        uint8_t* bytes;
        uint16_t name_length;
        uint16_t parameter_count;
        uint8_t return_type_code;
        uint32_t return_target_class_id;
        struct executor_parameter_view* parameters;
        const uint8_t* expression;
        uint32_t expression_length;
};

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

static void _text_parser_clear(struct executor_text_parser* parser) {
        size_t i;
        for (i = 0; i < parser->allocation_count; i++) {
                free(parser->allocations[i]);
        }
        free(parser->allocations);
}

static void _text_skip_space(struct executor_text_parser* parser) {
        while (parser->position < parser->length &&
               (parser->bytes[parser->position] == ' ' ||
                parser->bytes[parser->position] == '\t' ||
                parser->bytes[parser->position] == '\n' ||
                parser->bytes[parser->position] == '\r')) {
                parser->position++;
        }
}

static bool _resolve_identifier(struct executor_eval_context* context,
                                const uint8_t* name, size_t name_length,
                                struct executor_value* value) {
        size_t i;

        for (i = 0; i < context->binding_count; i++) {
                if (context->bindings[i].name_length == name_length &&
                    memcmp(context->bindings[i].name, name, name_length) == 0) {
                        *value = context->bindings[i].value;
                        return true;
                }
        }
        for (i = 0; i < context->schema_count; i++) {
                if (context->schema[i].name_length == name_length &&
                    memcmp(context->schema[i].name, name, name_length) == 0) {
                        return _value_from_bytes(
                            context->fields[i].type_code,
                            context->fields[i].value,
                            context->fields[i].value_length, value);
                }
        }
        return false;
}

static bool _parser_track_text(struct executor_text_parser* parser,
                               char* text) {
        char** grown =
            realloc(parser->allocations, (parser->allocation_count + 1) *
                                             sizeof *parser->allocations);
        if (grown == NULL) {
                free(text);
                return false;
        }
        parser->allocations                             = grown;
        parser->allocations[parser->allocation_count++] = text;
        return true;
}

static bool _parse_text_expression(struct executor_text_parser* parser,
                                   struct executor_value* result);

static bool _parse_text_primary(struct executor_text_parser* parser,
                                struct executor_value* result) {
        size_t start;
        uint8_t ch;

        _text_skip_space(parser);
        if (parser->position >= parser->length) {
                return false;
        }
        ch = parser->bytes[parser->position];
        if (ch == '(') {
                parser->position++;
                if (!_parse_text_expression(parser, result)) {
                        return false;
                }
                _text_skip_space(parser);
                if (parser->position >= parser->length ||
                    parser->bytes[parser->position++] != ')') {
                        return false;
                }
                return true;
        }
        if (ch == '"' || ch == '\'') {
                uint8_t quote = ch;
                char* text;
                size_t text_length = 0;
                size_t capacity    = parser->length - parser->position;

                parser->position++;
                text = malloc(capacity + 1);
                if (text == NULL) {
                        return false;
                }
                while (parser->position < parser->length &&
                       parser->bytes[parser->position] != quote) {
                        ch = parser->bytes[parser->position++];
                        if (ch == '\\') {
                                if (parser->position >= parser->length) {
                                        free(text);
                                        return false;
                                }
                                ch = parser->bytes[parser->position++];
                                if (ch != '\\' && ch != '"' && ch != '\'') {
                                        free(text);
                                        return false;
                                }
                        }
                        text[text_length++] = (char)ch;
                }
                if (parser->position >= parser->length) {
                        free(text);
                        return false;
                }
                parser->position++;
                text[text_length] = '\0';
                if (!_parser_track_text(parser, text)) {
                        return false;
                }
                result->type_code         = EXECUTOR_FORMAT_TYPE_TEXT;
                result->value.text.bytes  = (const uint8_t*)text;
                result->value.text.length = text_length;
                return true;
        }
        if ((ch >= '0' && ch <= '9')) {
                bool is_float = false;
                char* number;
                double floating;
                int64_t integer;

                start = parser->position;
                while (parser->position < parser->length) {
                        ch = parser->bytes[parser->position];
                        if (ch >= '0' && ch <= '9') {
                                parser->position++;
                        } else if (ch == '.' && !is_float) {
                                is_float = true;
                                parser->position++;
                        } else {
                                break;
                        }
                }
                number = malloc(parser->position - start + 1);
                if (number == NULL) {
                        return false;
                }
                memcpy(number, parser->bytes + start, parser->position - start);
                number[parser->position - start] = '\0';
                if (is_float) {
                        if (!_parse_double(number, &floating)) {
                                free(number);
                                return false;
                        }
                        result->type_code      = EXECUTOR_FORMAT_TYPE_FLOAT;
                        result->value.floating = floating;
                } else {
                        if (!_parse_i64(number, &integer)) {
                                free(number);
                                return false;
                        }
                        result->type_code     = EXECUTOR_FORMAT_TYPE_INTEGER;
                        result->value.integer = integer;
                }
                free(number);
                return true;
        }
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_') {
                start = parser->position++;
                while (parser->position < parser->length) {
                        ch = parser->bytes[parser->position];
                        if ((ch >= 'A' && ch <= 'Z') ||
                            (ch >= 'a' && ch <= 'z') ||
                            (ch >= '0' && ch <= '9') || ch == '_') {
                                parser->position++;
                        } else {
                                break;
                        }
                }
                if (parser->position - start == 4 &&
                    memcmp(parser->bytes + start, "true", 4) == 0) {
                        result->type_code     = EXECUTOR_FORMAT_TYPE_BOOL;
                        result->value.boolean = true;
                        return true;
                }
                if (parser->position - start == 5 &&
                    memcmp(parser->bytes + start, "false", 5) == 0) {
                        result->type_code     = EXECUTOR_FORMAT_TYPE_BOOL;
                        result->value.boolean = false;
                        return true;
                }
                return _resolve_identifier(parser->context,
                                           parser->bytes + start,
                                           parser->position - start, result);
        }
        return false;
}

static bool _apply_text_operator(enum ANALYZER_OPERATOR op,
                                 const struct executor_value* left,
                                 const struct executor_value* right,
                                 struct executor_value* result) {
        if (op <= ANALYZER_OPERATOR_DIVIDE) {
                double a;
                double b;
                bool ai;
                bool bi;
                if (!_numeric_value(left, &a, &ai) ||
                    !_numeric_value(right, &b, &bi)) {
                        return false;
                }
                if (ai && bi && op != ANALYZER_OPERATOR_DIVIDE) {
                        int64_t x = left->value.integer;
                        int64_t y = right->value.integer;
                        int64_t value;
                        if (op == ANALYZER_OPERATOR_ADD) {
                                if ((y > 0 && x > INT64_MAX - y) ||
                                    (y < 0 && x < INT64_MIN - y)) {
                                        return false;
                                }
                                value = x + y;
                        } else if (op == ANALYZER_OPERATOR_SUBTRACT) {
                                if ((y < 0 && x > INT64_MAX + y) ||
                                    (y > 0 && x < INT64_MIN + y)) {
                                        return false;
                                }
                                value = x - y;
                        } else {
                                if (!_multiply_i64(x, y, &value)) {
                                        return false;
                                }
                        }
                        result->type_code     = EXECUTOR_FORMAT_TYPE_INTEGER;
                        result->value.integer = value;
                        return true;
                }
                if (op == ANALYZER_OPERATOR_DIVIDE && b == 0) {
                        return false;
                }
                result->type_code = EXECUTOR_FORMAT_TYPE_FLOAT;
                switch (op) {
                        case ANALYZER_OPERATOR_ADD:
                                result->value.floating = a + b;
                                break;
                        case ANALYZER_OPERATOR_SUBTRACT:
                                result->value.floating = a - b;
                                break;
                        case ANALYZER_OPERATOR_MULTIPLY:
                                result->value.floating = a * b;
                                break;
                        case ANALYZER_OPERATOR_DIVIDE:
                                result->value.floating = a / b;
                                break;
                        default:
                                return false;
                }
                return true;
        }
        {
                int comparison;
                double a;
                double b;
                bool ai;
                bool bi;
                if (left->type_code == EXECUTOR_FORMAT_TYPE_INTEGER &&
                    right->type_code == EXECUTOR_FORMAT_TYPE_INTEGER) {
                        comparison =
                            left->value.integer < right->value.integer   ? -1
                            : left->value.integer > right->value.integer ? 1
                                                                         : 0;
                } else if (_numeric_value(left, &a, &ai) &&
                           _numeric_value(right, &b, &bi)) {
                        comparison = a < b ? -1 : a > b ? 1 : 0;
                } else if (left->type_code != right->type_code) {
                        return false;
                } else if (left->type_code == EXECUTOR_FORMAT_TYPE_TEXT) {
                        size_t common =
                            left->value.text.length < right->value.text.length
                                ? left->value.text.length
                                : right->value.text.length;
                        comparison = memcmp(left->value.text.bytes,
                                            right->value.text.bytes, common);
                        if (comparison == 0) {
                                comparison = left->value.text.length <
                                                     right->value.text.length
                                                 ? -1
                                             : left->value.text.length >
                                                     right->value.text.length
                                                 ? 1
                                                 : 0;
                        }
                } else if (left->type_code == EXECUTOR_FORMAT_TYPE_BOOL) {
                        comparison = (int)left->value.boolean -
                                     (int)right->value.boolean;
                } else if (left->type_code == EXECUTOR_FORMAT_TYPE_POINTER) {
                        comparison = left->value.pointer.class_id <
                                             right->value.pointer.class_id
                                         ? -1
                                     : left->value.pointer.class_id >
                                             right->value.pointer.class_id
                                         ? 1
                                     : left->value.pointer.object_id <
                                             right->value.pointer.object_id
                                         ? -1
                                     : left->value.pointer.object_id >
                                             right->value.pointer.object_id
                                         ? 1
                                         : 0;
                } else {
                        return false;
                }
                result->type_code = EXECUTOR_FORMAT_TYPE_BOOL;
                switch (op) {
                        case ANALYZER_OPERATOR_EQUAL:
                                result->value.boolean = comparison == 0;
                                break;
                        case ANALYZER_OPERATOR_NOT_EQUAL:
                                result->value.boolean = comparison != 0;
                                break;
                        case ANALYZER_OPERATOR_LESS:
                                result->value.boolean = comparison < 0;
                                break;
                        case ANALYZER_OPERATOR_LESS_EQUAL:
                                result->value.boolean = comparison <= 0;
                                break;
                        case ANALYZER_OPERATOR_GREATER:
                                result->value.boolean = comparison > 0;
                                break;
                        case ANALYZER_OPERATOR_GREATER_EQUAL:
                                result->value.boolean = comparison >= 0;
                                break;
                        default:
                                return false;
                }
                return true;
        }
}

static bool _parse_text_unary(struct executor_text_parser* parser,
                              struct executor_value* result) {
        _text_skip_space(parser);
        if (parser->position < parser->length &&
            parser->bytes[parser->position] == '-') {
                struct executor_value operand;
                parser->position++;
                if (!_parse_text_unary(parser, &operand)) {
                        return false;
                }
                if (operand.type_code == EXECUTOR_FORMAT_TYPE_INTEGER) {
                        if (operand.value.integer == INT64_MIN) {
                                return false;
                        }
                        result->type_code     = EXECUTOR_FORMAT_TYPE_INTEGER;
                        result->value.integer = -operand.value.integer;
                        return true;
                }
                if (operand.type_code == EXECUTOR_FORMAT_TYPE_FLOAT) {
                        result->type_code      = EXECUTOR_FORMAT_TYPE_FLOAT;
                        result->value.floating = -operand.value.floating;
                        return true;
                }
                return false;
        }
        return _parse_text_primary(parser, result);
}

static bool _parse_text_term(struct executor_text_parser* parser,
                             struct executor_value* result) {
        struct executor_value left;

        if (!_parse_text_unary(parser, &left)) {
                return false;
        }
        for (;;) {
                struct executor_value right;
                struct executor_value combined;
                enum ANALYZER_OPERATOR op;
                uint8_t ch;

                _text_skip_space(parser);
                if (parser->position >= parser->length) {
                        break;
                }
                ch = parser->bytes[parser->position];
                if (ch != '*' && ch != '/') {
                        break;
                }
                parser->position++;
                op = ch == '*' ? ANALYZER_OPERATOR_MULTIPLY
                               : ANALYZER_OPERATOR_DIVIDE;
                if (!_parse_text_unary(parser, &right) ||
                    !_apply_text_operator(op, &left, &right, &combined)) {
                        return false;
                }
                left = combined;
        }
        *result = left;
        return true;
}

static bool _parse_text_additive(struct executor_text_parser* parser,
                                 struct executor_value* result) {
        struct executor_value left;

        if (!_parse_text_term(parser, &left)) {
                return false;
        }
        for (;;) {
                struct executor_value right;
                struct executor_value combined;
                enum ANALYZER_OPERATOR op;
                uint8_t ch;

                _text_skip_space(parser);
                if (parser->position >= parser->length) {
                        break;
                }
                ch = parser->bytes[parser->position];
                if (ch != '+' && ch != '-') {
                        break;
                }
                parser->position++;
                op = ch == '+' ? ANALYZER_OPERATOR_ADD
                               : ANALYZER_OPERATOR_SUBTRACT;
                if (!_parse_text_term(parser, &right) ||
                    !_apply_text_operator(op, &left, &right, &combined)) {
                        return false;
                }
                left = combined;
        }
        *result = left;
        return true;
}

static bool _parse_text_expression(struct executor_text_parser* parser,
                                   struct executor_value* result) {
        struct executor_value left;
        struct executor_value right;
        struct executor_value combined;
        enum ANALYZER_OPERATOR op;
        uint8_t ch;

        if (!_parse_text_additive(parser, &left)) {
                return false;
        }
        _text_skip_space(parser);
        if (parser->position >= parser->length) {
                *result = left;
                return true;
        }
        ch = parser->bytes[parser->position];
        if (ch == '=') {
                parser->position++;
                if (parser->position < parser->length &&
                    parser->bytes[parser->position] == '=') {
                        return false;
                }
                op = ANALYZER_OPERATOR_EQUAL;
        } else if (ch == '!') {
                if (parser->position + 1 >= parser->length ||
                    parser->bytes[parser->position + 1] != '=') {
                        return false;
                }
                parser->position += 2;
                op = ANALYZER_OPERATOR_NOT_EQUAL;
        } else if (ch == '<' || ch == '>') {
                parser->position++;
                if (parser->position < parser->length &&
                    parser->bytes[parser->position] == '=') {
                        parser->position++;
                        op = ch == '<' ? ANALYZER_OPERATOR_LESS_EQUAL
                                       : ANALYZER_OPERATOR_GREATER_EQUAL;
                } else {
                        op = ch == '<' ? ANALYZER_OPERATOR_LESS
                                       : ANALYZER_OPERATOR_GREATER;
                }
        } else {
                *result = left;
                return true;
        }
        if (!_parse_text_additive(parser, &right) ||
            !_apply_text_operator(op, &left, &right, &combined)) {
                return false;
        }
        _text_skip_space(parser);
        if (parser->position != parser->length) {
                return false;
        }
        *result = combined;
        return true;
}

static bool _evaluate_text_expression(const uint8_t* expression,
                                      size_t expression_length,
                                      struct executor_eval_context* context,
                                      struct executor_value* result) {
        struct executor_text_parser parser;
        bool ok;

        memset(&parser, 0, sizeof parser);
        parser.bytes   = expression;
        parser.length  = expression_length;
        parser.context = context;
        ok             = _parse_text_expression(&parser, result);
        if (ok) {
                _text_skip_space(&parser);
                ok = parser.position == parser.length;
        }
        _text_parser_clear(&parser);
        return ok;
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

static bool _class_name_by_id(struct executor_format_file* file,
                              uint32_t class_id, char** name) {
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

static bool _print_value(struct executor_format_file* file,
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

static enum EXECUTOR_RESULT _call_function(
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

static enum EXECUTOR_RESULT _find_objects(
    struct database* db, const struct analyzer_statement* statement) {
        struct executor_format_file file;
        struct executor_format_class_entry entry;
        struct executor_format_field* schema = NULL;
        uint8_t* segment                     = NULL;
        size_t schema_count                  = 0;
        size_t length;
        size_t offset               = 0;
        enum EXECUTOR_RESULT result = EXECUTOR_ERROR_STORAGE_FAILED;

        if (statement->name == NULL || !db->format_mode ||
            !executor_format_file_load(db, &file)) {
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }
        if (!_find_class(&file, statement->name, &entry)) {
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }
        if (!_collect_schema_fields(&file, entry.class_id, &schema,
                                    &schema_count) ||
            entry.data_length > SIZE_MAX ||
            !executor_helper_range_valid(file.size, entry.data_offset,
                                         entry.data_length)) {
                _free_schema_fields(schema, schema_count);
                return EXECUTOR_ERROR_STORAGE_FAILED;
        }
        length = (size_t)entry.data_length;
        if (length != 0) {
                segment = malloc(length);
                if (segment == NULL ||
                    !executor_format_file_read(&file, entry.data_offset,
                                               segment, length)) {
                        free(segment);
                        _free_schema_fields(schema, schema_count);
                        return EXECUTOR_ERROR_STORAGE_FAILED;
                }
        }
        while (offset < length) {
                struct executor_format_object_header header;
                struct executor_format_object_field* fields = NULL;
                uint8_t* object_segment                     = NULL;
                struct executor_value condition;
                size_t i;
                bool matches = true;

                if (!executor_helper_read_object_header(
                        segment + offset, length - offset, &header) ||
                    header.record_length > length - offset ||
                    !_load_object_fields(&file, &entry, header.object_id,
                                         schema, schema_count, &object_segment,
                                         NULL, &header, &fields)) {
                        result = EXECUTOR_ERROR_STORAGE_FAILED;
                        goto done;
                }
                free(object_segment);
                if (statement->expression != NULL) {
                        if (!_evaluate_ast(statement->expression, schema,
                                           schema_count, fields, &condition) ||
                            condition.type_code != EXECUTOR_FORMAT_TYPE_BOOL) {
                                _free_object_fields(fields, schema_count);
                                result = EXECUTOR_ERROR_BAD_STATEMENT;
                                goto done;
                        }
                        matches = condition.value.boolean;
                }
                if (matches) {
                        if (printf("%s.%llu", statement->name,
                                   (unsigned long long)header.object_id) < 0) {
                                _free_object_fields(fields, schema_count);
                                result = EXECUTOR_ERROR_STORAGE_FAILED;
                                goto done;
                        }
                        for (i = 0; i < schema_count; i++) {
                                struct executor_value value;
                                if (!_value_from_bytes(
                                        fields[i].type_code, fields[i].value,
                                        fields[i].value_length, &value) ||
                                    printf(" %s=", schema[i].name) < 0 ||
                                    !_print_value(&file, &value)) {
                                        _free_object_fields(fields,
                                                            schema_count);
                                        result = EXECUTOR_ERROR_STORAGE_FAILED;
                                        goto done;
                                }
                        }
                        if (putchar('\n') == EOF) {
                                _free_object_fields(fields, schema_count);
                                result = EXECUTOR_ERROR_STORAGE_FAILED;
                                goto done;
                        }
                }
                _free_object_fields(fields, schema_count);
                offset += header.record_length;
        }
        result = EXECUTOR_OK;
done:
        free(segment);
        _free_schema_fields(schema, schema_count);
        return result;
}

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

static enum EXECUTOR_RESULT _not_implemented(
    struct database* db, const struct analyzer_statement* statement) {
        (void)db;
        (void)statement;
        return EXECUTOR_ERROR_NOT_IMPLEMENTED;
}

static bool _kind_matches_group(const struct analyzer_statement* statement) {
        if (statement == NULL) {
                return false;
        }
        switch (statement->group) {
                case ANALYZER_GROUP_SCHEMA:
                        return statement->kind == ANALYZER_CREATE_CLASS ||
                               statement->kind == ANALYZER_DROP_CLASS ||
                               statement->kind ==
                                   ANALYZER_ALTER_CLASS_ADD_FIELD ||
                               statement->kind ==
                                   ANALYZER_ALTER_CLASS_DROP_FIELD ||
                               statement->kind ==
                                   ANALYZER_ALTER_CLASS_RENAME_FIELD ||
                               statement->kind == ANALYZER_ALTER_CLASS_RENAME ||
                               statement->kind ==
                                   ANALYZER_ALTER_CLASS_EXTENDS ||
                               statement->kind ==
                                   ANALYZER_ALTER_CLASS_IMPLEMENTS ||
                               statement->kind == ANALYZER_CREATE_INTERFACE ||
                               statement->kind == ANALYZER_DROP_INTERFACE;
                case ANALYZER_GROUP_FUNCTION:
                        return statement->kind == ANALYZER_CREATE_FUNCTION ||
                               statement->kind == ANALYZER_DROP_FUNCTION ||
                               statement->kind == ANALYZER_CALL;
                case ANALYZER_GROUP_OBJECT:
                        return statement->kind == ANALYZER_NEW ||
                               statement->kind == ANALYZER_FIND ||
                               statement->kind == ANALYZER_SET ||
                               statement->kind == ANALYZER_DELETE;
                case ANALYZER_GROUP_METADATA:
                        return statement->kind == ANALYZER_LIST_CLASSES ||
                               statement->kind == ANALYZER_LIST_INTERFACES ||
                               statement->kind == ANALYZER_DESCRIBE;
                default:
                        return false;
        }
}

enum EXECUTOR_RESULT i_execute(struct database* db,
                               const struct analyzer_statement* statement) {
        enum EXECUTOR_RESULT result = EXECUTOR_ERROR_BAD_STATEMENT;

        if (db == NULL || !_kind_matches_group(statement)) {
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }
        switch (statement->group) {
                case ANALYZER_GROUP_SCHEMA:
                        switch (statement->kind) {
                                case ANALYZER_CREATE_CLASS:
                                        result = _create_class(db, statement);
                                        break;
                                case ANALYZER_CREATE_INTERFACE:
                                        result =
                                            _create_interface(db, statement);
                                        break;
                                case ANALYZER_DROP_CLASS:
                                        result = _drop_class(db, statement);
                                        break;
                                case ANALYZER_DROP_INTERFACE:
                                        result = _drop_interface(db, statement);
                                        break;
                                case ANALYZER_ALTER_CLASS_ADD_FIELD:
                                case ANALYZER_ALTER_CLASS_DROP_FIELD:
                                case ANALYZER_ALTER_CLASS_RENAME_FIELD:
                                case ANALYZER_ALTER_CLASS_RENAME:
                                case ANALYZER_ALTER_CLASS_EXTENDS:
                                case ANALYZER_ALTER_CLASS_IMPLEMENTS:
                                        result = _alter_class(db, statement);
                                        break;
                                default:
                                        result =
                                            _not_implemented(db, statement);
                                        break;
                        }
                        break;
                case ANALYZER_GROUP_FUNCTION:
                        switch (statement->kind) {
                                case ANALYZER_CREATE_FUNCTION:
                                        result =
                                            _create_function(db, statement);
                                        break;
                                case ANALYZER_DROP_FUNCTION:
                                        result = _drop_function(db, statement);
                                        break;
                                case ANALYZER_CALL:
                                        result = _call_function(db, statement);
                                        break;
                                default:
                                        result =
                                            _not_implemented(db, statement);
                                        break;
                        }
                        break;
                case ANALYZER_GROUP_OBJECT:
                        if (statement->kind == ANALYZER_NEW) {
                                result = _new_object(db, statement);
                        } else if (statement->kind == ANALYZER_FIND) {
                                result = _find_objects(db, statement);
                        } else if (statement->kind == ANALYZER_SET) {
                                result = _set_object(db, statement);
                        } else if (statement->kind == ANALYZER_DELETE) {
                                result = _delete_object(db, statement);
                        } else {
                                result = _not_implemented(db, statement);
                        }
                        break;
                case ANALYZER_GROUP_METADATA:
                        switch (statement->kind) {
                                case ANALYZER_LIST_CLASSES:
                                        result = _list_entries(db, true);
                                        break;
                                case ANALYZER_LIST_INTERFACES:
                                        result = _list_entries(db, false);
                                        break;
                                case ANALYZER_DESCRIBE:
                                        result = _describe_class(db, statement);
                                        break;
                                default:
                                        result =
                                            _not_implemented(db, statement);
                                        break;
                        }
                        break;
                default:
                        return EXECUTOR_ERROR_BAD_STATEMENT;
        }
        if (result == EXECUTOR_OK && database_is_modified(db) &&
            !database_save(db)) {
                result = EXECUTOR_ERROR_SAVE_FAILED;
        }
        return result;
}
