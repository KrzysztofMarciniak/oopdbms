
#include "implementation/executor/helper/interface_definition/main.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "implementation/executor/helper/basic/main.h"
#include "implementation/executor/helper/binary_primitives/main.h"
#include "implementation/executor/helper/type_name/main.h"
#include "interface/storage/page.h"

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

enum EXECUTOR_RESULT helper_create_interface(
    struct database* db, const struct analyzer_statement* statement) {
        size_t i;
        uint32_t existing_page;
        uint16_t existing_offset;
        uint16_t existing_length;
        uint32_t definition_page            = 0;
        bool page_allocated                 = false;
        helper_interface_page_field* fields = NULL;
        char* type_names                    = NULL;
        helper_interface_page_definition definition;
        uint16_t definition_length;
        enum EXECUTOR_RESULT result = EXECUTOR_ERROR_BAD_STATEMENT;

        if (db == NULL || statement == NULL ||
            statement->kind != ANALYZER_CREATE_INTERFACE ||
            statement->group != ANALYZER_GROUP_SCHEMA ||
            statement->name == NULL || statement->name[0] == '\0' ||
            strlen(statement->name) > DB_PAGE_SIZE - DIR_PAGE_HDR_SIZE - 10u ||
            statement->fields.count > UINT16_MAX ||
            (statement->fields.count > 0 && statement->fields.items == NULL) ||
            db->hdr.iface_count == UINT32_MAX) {
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }

        if (db_interface_dir_find(db, statement->name, &existing_page,
                                  &existing_offset, &existing_length)) {
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }

        if (statement->fields.count > 0) {
                fields     = calloc(statement->fields.count, sizeof(*fields));
                type_names = calloc(statement->fields.count, DB_PAGE_SIZE);
                if (fields == NULL || type_names == NULL) {
                        goto cleanup;
                }
        }

        for (i = 0; i < statement->fields.count; ++i) {
                const struct analyzer_field* field =
                    &statement->fields.items[i];
                size_t j;

                if (field->name == NULL || field->name[0] == '\0') {
                        goto cleanup;
                }

                fields[i].type_name = type_names + i * DB_PAGE_SIZE;
                fields[i].name      = field->name;
                if (helper_type_name(&field->type, (char*)fields[i].type_name,
                                     DB_PAGE_SIZE) == 0) {
                        goto cleanup;
                }

                for (j = 0; j < i; ++j) {
                        if (strcmp(field->name,
                                   statement->fields.items[j].name) == 0) {
                                goto cleanup;
                        }
                }
        }

        definition.field_count = (uint16_t)statement->fields.count;
        definition.fields      = fields;
        if (!db_alloc_page(db, &definition_page)) {
                result = EXECUTOR_ERROR_STORAGE_FAILED;
                goto cleanup;
        }
        page_allocated = true;

        if (helper_write_interface_page_definition(
                db->page_buf, sizeof(db->page_buf), &definition,
                &definition_length) != 0) {
                goto cleanup;
        }
        if (!db_write_page(db, definition_page, db->page_buf) ||
            !db_interface_dir_put(db, statement->name, definition_page, 0,
                                  definition_length, false)) {
                result = EXECUTOR_ERROR_STORAGE_FAILED;
                goto cleanup;
        }

        db->hdr.iface_count++;
        database_mark_modified(db);
        result = EXECUTOR_OK;

cleanup:
        if (result != EXECUTOR_OK && page_allocated) {
                db_free_page(db, definition_page);
        }
        free(type_names);
        free(fields);
        return result;
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
