
#include "implementation/executor/helper/interface_definition/main.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "implementation/executor/helper/basic/main.h"
#include "implementation/executor/helper/binary_primitives/main.h"

static int helper_valid_type_code(uint8_t type_code) {
        return type_code >= HELPER_TYPE_INTEGER &&
               type_code <= HELPER_TYPE_POINTER;
}

static int helper_read_name(FILE* file, uint16_t length, char** name) {
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

int helper_write_interface_definition(
    FILE* file, const helper_interface_definition* definition) {
        size_t name_length;
        size_t i;

        if (file == NULL || definition == NULL || definition->name == NULL) {
                return -1;
        }

        if (definition->field_count > 0 && definition->fields == NULL) {
                return -1;
        }

        name_length = strlen(definition->name);

        if (name_length > UINT16_MAX) {
                return -1;
        }

        /* Interface definition header. */
        if (helper_write_u16(file, (uint16_t)name_length) != 0) {
                return -1;
        }

        if (helper_write_u16(file, definition->field_count) != 0) {
                return -1;
        }

        /* Interface name, without a NUL terminator on disk. */
        if (helper_write_bytes(file, definition->name, name_length) != 0) {
                return -1;
        }

        /* Field descriptors. */
        for (i = 0; i < definition->field_count; ++i) {
                const helper_interface_field* field = &definition->fields[i];
                size_t field_name_length;

                if (field->name == NULL ||
                    !helper_valid_type_code(field->type_code)) {
                        return -1;
                }

                field_name_length = strlen(field->name);

                if (field_name_length > UINT16_MAX) {
                        return -1;
                }

                if (helper_write_u32(file, field->field_id) != 0) {
                        return -1;
                }

                if (helper_write_u8(file, field->type_code) != 0) {
                        return -1;
                }

                if (helper_write_u16(file, (uint16_t)field_name_length) != 0) {
                        return -1;
                }

                if (field->type_code == HELPER_TYPE_POINTER) {
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

        return 0;
}

int helper_write_interface_page_definition(
    uint8_t* buffer, size_t buffer_size,
    const helper_interface_page_definition* definition, uint16_t* written) {
        size_t offset = 0;
        size_t i;

        if (buffer == NULL || definition == NULL || written == NULL ||
            buffer_size < sizeof(uint16_t) ||
            (definition->field_count > 0 && definition->fields == NULL)) {
                return -1;
        }

        buffer[offset++] = (uint8_t)definition->field_count;
        buffer[offset++] = (uint8_t)(definition->field_count >> 8);

        for (i = 0; i < definition->field_count; ++i) {
                const helper_interface_page_field* field =
                    &definition->fields[i];
                size_t type_length;
                size_t name_length;
                size_t field_length;

                if (field->type_name == NULL || field->name == NULL) {
                        return -1;
                }

                type_length = strlen(field->type_name);
                name_length = strlen(field->name);
                if (type_length == 0 || type_length > UINT16_MAX ||
                    name_length == 0 || name_length > UINT16_MAX) {
                        return -1;
                }

                field_length = sizeof(uint16_t) * 2 + type_length + name_length;
                if (field_length > buffer_size - offset) {
                        return -1;
                }

                buffer[offset++] = (uint8_t)type_length;
                buffer[offset++] = (uint8_t)(type_length >> 8);
                memcpy(buffer + offset, field->type_name, type_length);
                offset += type_length;
                buffer[offset++] = (uint8_t)name_length;
                buffer[offset++] = (uint8_t)(name_length >> 8);
                memcpy(buffer + offset, field->name, name_length);
                offset += name_length;
        }

        if (offset > UINT16_MAX) {
                return -1;
        }
        *written = (uint16_t)offset;
        return 0;
}

void helper_free_interface_definition(helper_interface_definition* definition) {
        size_t i;

        if (definition == NULL) {
                return;
        }

        free(definition->name);
        definition->name = NULL;

        if (definition->fields != NULL) {
                for (i = 0; i < definition->field_count; ++i) {
                        free(definition->fields[i].name);
                        definition->fields[i].name = NULL;
                }

                free(definition->fields);
                definition->fields = NULL;
        }

        definition->field_count = 0;
}

int helper_read_interface_definition(FILE* file,
                                     helper_interface_definition* definition) {
        helper_interface_definition result = {0};
        uint16_t name_length;
        size_t i;

        if (file == NULL || definition == NULL) {
                return -1;
        }

        /*
         * The format stores name_length and field_count before the name.
         * Read both before consuming the name bytes.
         */
        if (helper_read_u16(file, &name_length) != 0) {
                goto failure;
        }

        if (helper_read_u16(file, &result.field_count) != 0) {
                goto failure;
        }

        if (helper_read_name(file, name_length, &result.name) != 0) {
                goto failure;
        }

        if (result.field_count > 0) {
                result.fields =
                    calloc(result.field_count, sizeof(*result.fields));

                if (result.fields == NULL) {
                        goto failure;
                }
        }

        for (i = 0; i < result.field_count; ++i) {
                helper_interface_field* field = &result.fields[i];
                uint16_t field_name_length;

                if (helper_read_u32(file, &field->field_id) != 0) {
                        goto failure;
                }

                if (helper_read_u8(file, &field->type_code) != 0) {
                        goto failure;
                }

                if (!helper_valid_type_code(field->type_code)) {
                        goto failure;
                }

                if (helper_read_u16(file, &field_name_length) != 0) {
                        goto failure;
                }

                if (field->type_code == HELPER_TYPE_POINTER) {
                        if (helper_read_u32(file, &field->target_class_id) !=
                            0) {
                                goto failure;
                        }
                }

                field->name = malloc((size_t)field_name_length + 1u);
                if (field->name == NULL) {
                        goto failure;
                }

                if (helper_read_bytes(file, field->name, field_name_length) !=
                    0) {
                        goto failure;
                }

                field->name[field_name_length] = '\0';
        }

        *definition = result;
        return 0;

failure:
        helper_free_interface_definition(&result);
        return -1;
}
