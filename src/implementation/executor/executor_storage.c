/**
 * @file executor_storage.c
 * @brief Executor storage implementation.
 */

#include "implementation/executor/executor_internal.h"

bool _name_is(const uint8_t* definition, size_t length,
              size_t name_header_offset, size_t name_offset, const char* name) {
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

bool _read_definition(const struct executor_format_file* file, uint64_t offset,
                      uint32_t length, uint8_t** definition) {
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

bool _find_class(struct executor_format_file* file, const char* name,
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

bool _find_interface(struct executor_format_file* file, const char* name,
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

uint8_t _format_type(const struct analyzer_type* type) {
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

bool _collect_fields(struct executor_format_file* file,
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

bool _next_id(uint32_t max_id, uint32_t* next_id) {
        if (max_id == UINT32_MAX) {
                return false;
        }
        *next_id = max_id + 1;
        if (*next_id == 0) {
                *next_id = 1;
        }
        return true;
}

bool _max_class_id(struct executor_format_file* file, uint32_t* max_id) {
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

bool _max_interface_id(struct executor_format_file* file, uint32_t* max_id) {
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

bool _max_field_id(struct executor_format_file* file, uint32_t* max_field_id) {
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

bool _append_new_table(struct executor_format_file* file,
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

bool _append_table_bytes(struct executor_format_file* file,
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

bool _parse_i64(const char* text, int64_t* value) {
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

bool _parse_u64(const char* text, uint64_t* value) {
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

bool _parse_double(const char* text, double* value) {
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
