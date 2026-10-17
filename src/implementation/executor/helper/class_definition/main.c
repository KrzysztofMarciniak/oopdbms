#include "implementation/executor/helper/class_definition/main.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "implementation/executor/helper/basic/main.h"
#include "implementation/executor/helper/binary_primitives/main.h"

static int helper_class_valid_type_code(uint8_t type_code) {
        return type_code >= HELPER_CLASS_TYPE_INTEGER &&
               type_code <= HELPER_CLASS_TYPE_POINTER;
}

static int helper_class_read_name(FILE* file, uint16_t length, char** name) {
        char* result;

        if (file == NULL || name == NULL) {
                return -1;
        }

        result = malloc((size_t)length + 1u);
        if (result == NULL) {
                return -1;
        }

        if (helper_read_bytes(file, result, length) != 0) {
                free(result);
                return -1;
        }

        result[length] = '\0';
        *name          = result;

        return 0;
}

static int helper_class_validate_definition(
    const helper_class_definition* definition) {
        size_t i;

        if (definition == NULL || definition->name == NULL) {
                return -1;
        }

        if (definition->field_count > 0 && definition->fields == NULL) {
                return -1;
        }

        if (definition->interface_count > 0 &&
            definition->interface_ids == NULL) {
                return -1;
        }

        if (strlen(definition->name) > UINT16_MAX) {
                return -1;
        }

        for (i = 0; i < (size_t)definition->field_count; ++i) {
                const helper_class_field* field = &definition->fields[i];

                if (field->name == NULL ||
                    !helper_class_valid_type_code(field->type_code)) {
                        return -1;
                }

                if (strlen(field->name) > UINT16_MAX) {
                        return -1;
                }
        }

        return 0;
}

static int helper_class_page_write_u16(uint8_t* buffer, size_t buffer_size,
                                       size_t* offset, uint16_t value) {
        if (buffer == NULL || offset == NULL || *offset > buffer_size ||
            buffer_size - *offset < sizeof(value)) {
                return -1;
        }

        buffer[(*offset)++] = (uint8_t)value;
        buffer[(*offset)++] = (uint8_t)(value >> 8);
        return 0;
}

static int helper_class_page_write_string(uint8_t* buffer, size_t buffer_size,
                                          size_t* offset, const char* value) {
        size_t length;

        if (buffer == NULL || offset == NULL || value == NULL) {
                return -1;
        }

        length = strlen(value);
        if (length == 0 || length > UINT16_MAX || *offset > buffer_size ||
            buffer_size - *offset < length) {
                return -1;
        }

        memcpy(buffer + *offset, value, length);
        *offset += length;
        return 0;
}

int helper_write_class_page_definition(
    uint8_t* buffer, size_t buffer_size,
    const helper_class_page_definition* definition, uint16_t* written) {
        size_t offset = 0;
        size_t i;
        size_t parent_length;

        if (buffer == NULL || definition == NULL || written == NULL ||
            (definition->field_count > 0 && definition->fields == NULL) ||
            (definition->interface_count > 0 &&
             definition->interfaces == NULL) ||
            buffer_size < sizeof(uint16_t)) {
                return -1;
        }

        if (helper_class_page_write_u16(buffer, buffer_size, &offset,
                                        definition->field_count) != 0) {
                return -1;
        }

        for (i = 0; i < (size_t)definition->field_count; ++i) {
                const helper_class_page_field* field = &definition->fields[i];
                size_t type_length;
                size_t name_length;

                if (field->type_name == NULL || field->name == NULL) {
                        return -1;
                }
                type_length = strlen(field->type_name);
                name_length = strlen(field->name);
                if (type_length == 0 || type_length > UINT16_MAX ||
                    name_length == 0 || name_length > UINT16_MAX ||
                    helper_class_page_write_u16(buffer, buffer_size, &offset,
                                                (uint16_t)type_length) != 0 ||
                    helper_class_page_write_string(buffer, buffer_size, &offset,
                                                   field->type_name) != 0 ||
                    helper_class_page_write_u16(buffer, buffer_size, &offset,
                                                (uint16_t)name_length) != 0 ||
                    helper_class_page_write_string(buffer, buffer_size, &offset,
                                                   field->name) != 0) {
                        return -1;
                }
        }

        parent_length = definition->parent_name == NULL
                            ? 0
                            : strlen(definition->parent_name);
        if (parent_length > UINT16_MAX ||
            helper_class_page_write_u16(buffer, buffer_size, &offset,
                                        (uint16_t)parent_length) != 0) {
                return -1;
        }
        if (parent_length > 0 &&
            helper_class_page_write_string(buffer, buffer_size, &offset,
                                           definition->parent_name) != 0) {
                return -1;
        }

        if (helper_class_page_write_u16(buffer, buffer_size, &offset,
                                        definition->interface_count) != 0) {
                return -1;
        }
        for (i = 0; i < (size_t)definition->interface_count; ++i) {
                size_t interface_length;

                if (definition->interfaces[i] == NULL) {
                        return -1;
                }
                interface_length = strlen(definition->interfaces[i]);
                if (interface_length == 0 || interface_length > UINT16_MAX ||
                    helper_class_page_write_u16(buffer, buffer_size, &offset,
                                                (uint16_t)interface_length) !=
                        0 ||
                    helper_class_page_write_string(buffer, buffer_size, &offset,
                                                   definition->interfaces[i]) !=
                        0) {
                        return -1;
                }
        }

        if (offset > UINT16_MAX) {
                return -1;
        }
        *written = (uint16_t)offset;
        return 0;
}

int helper_write_class_definition(FILE* file,
                                  const helper_class_definition* definition) {
        size_t name_length;
        size_t i;

        if (file == NULL || helper_class_validate_definition(definition) != 0) {
                return -1;
        }

        name_length = strlen(definition->name);

        /* Fixed 16-byte class definition header. */
        if (helper_write_u16(file, (uint16_t)name_length) != 0) {
                return -1;
        }

        if (helper_write_u16(file, definition->flags) != 0) {
                return -1;
        }

        if (helper_write_u32(file, definition->parent_class_id) != 0) {
                return -1;
        }

        if (helper_write_u32(file, definition->field_count) != 0) {
                return -1;
        }

        if (helper_write_u32(file, definition->interface_count) != 0) {
                return -1;
        }

        /* Class name bytes; no NUL terminator is stored. */
        if (helper_write_bytes(file, definition->name, name_length) != 0) {
                return -1;
        }

        /* Field descriptors. */
        for (i = 0; i < (size_t)definition->field_count; ++i) {
                const helper_class_field* field = &definition->fields[i];
                size_t field_name_length        = strlen(field->name);

                if (helper_write_u32(file, field->field_id) != 0) {
                        return -1;
                }

                if (helper_write_u8(file, field->type_code) != 0) {
                        return -1;
                }

                if (helper_write_u16(file, (uint16_t)field_name_length) != 0) {
                        return -1;
                }

                if (field->type_code == HELPER_CLASS_TYPE_POINTER) {
                        if (helper_write_u32(file, field->target_class_id) !=
                            0) {
                                return -1;
                        }
                }

                if (helper_write_bytes(file, field->name, field_name_length) !=
                    0) {
                        return -1;
                }
        }

        /* Implemented interface IDs. */
        for (i = 0; i < (size_t)definition->interface_count; ++i) {
                if (helper_write_u32(file, definition->interface_ids[i]) != 0) {
                        return -1;
                }
        }

        return 0;
}

void helper_free_class_definition(helper_class_definition* definition) {
        size_t i;

        if (definition == NULL) {
                return;
        }

        free(definition->name);
        definition->name = NULL;

        if (definition->fields != NULL) {
                for (i = 0; i < (size_t)definition->field_count; ++i) {
                        free(definition->fields[i].name);
                        definition->fields[i].name = NULL;
                }

                free(definition->fields);
                definition->fields = NULL;
        }

        free(definition->interface_ids);
        definition->interface_ids = NULL;

        definition->flags           = 0;
        definition->parent_class_id = 0;
        definition->field_count     = 0;
        definition->interface_count = 0;
}

int helper_read_class_definition(FILE* file,
                                 helper_class_definition* definition) {
        helper_class_definition result = {0};
        uint16_t name_length;
        size_t i;

        if (file == NULL || definition == NULL) {
                return -1;
        }

        /*
         * The complete fixed header comes before the class name bytes.
         */
        if (helper_read_u16(file, &name_length) != 0) {
                goto failure;
        }

        if (helper_read_u16(file, &result.flags) != 0) {
                goto failure;
        }

        if (helper_read_u32(file, &result.parent_class_id) != 0) {
                goto failure;
        }

        if (helper_read_u32(file, &result.field_count) != 0) {
                goto failure;
        }

        if (helper_read_u32(file, &result.interface_count) != 0) {
                goto failure;
        }

        if (helper_class_read_name(file, name_length, &result.name) != 0) {
                goto failure;
        }

        if (result.field_count > 0) {
                result.fields =
                    calloc((size_t)result.field_count, sizeof(*result.fields));

                if (result.fields == NULL) {
                        goto failure;
                }
        }

        if (result.interface_count > 0) {
                result.interface_ids = calloc((size_t)result.interface_count,
                                              sizeof(*result.interface_ids));

                if (result.interface_ids == NULL) {
                        goto failure;
                }
        }

        /* Read field descriptors. */
        for (i = 0; i < (size_t)result.field_count; ++i) {
                helper_class_field* field = &result.fields[i];
                uint16_t field_name_length;

                if (helper_read_u32(file, &field->field_id) != 0) {
                        goto failure;
                }

                if (helper_read_u8(file, &field->type_code) != 0) {
                        goto failure;
                }

                if (!helper_class_valid_type_code(field->type_code)) {
                        goto failure;
                }

                if (helper_read_u16(file, &field_name_length) != 0) {
                        goto failure;
                }

                if (field->type_code == HELPER_CLASS_TYPE_POINTER) {
                        if (helper_read_u32(file, &field->target_class_id) !=
                            0) {
                                goto failure;
                        }
                }

                if (helper_class_read_name(file, field_name_length,
                                           &field->name) != 0) {
                        goto failure;
                }
        }

        /* Read implemented interface IDs. */
        for (i = 0; i < (size_t)result.interface_count; ++i) {
                if (helper_read_u32(file, &result.interface_ids[i]) != 0) {
                        goto failure;
                }
        }

        *definition = result;
        return 0;

failure:
        helper_free_class_definition(&result);
        return -1;
}
