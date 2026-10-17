/**
 * @file create_interface.c
 * @brief CREATE INTERFACE <n> (<field> <type>[, ...])
 */

#include <stdint.h>
#include <string.h>

#include "implementation/executor/executor_internal.h"
#include "interface/storage/page.h"

enum EXECUTOR_RESULT execute_create_interface(
    struct database* db, const struct analyzer_statement* statement) {
        size_t definition_length = sizeof(uint16_t);
        size_t i;
        uint32_t existing_page;
        uint16_t existing_off;
        uint16_t existing_len;
        uint32_t definition_page;
        uint8_t* definition;

        if (db == NULL || statement == NULL || statement->name == NULL ||
            statement->name[0] == '\0' ||
            statement->fields.count > UINT16_MAX ||
            (statement->fields.count > 0 && statement->fields.items == NULL)) {
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }

        if (db_interface_dir_find(db, statement->name, &existing_page,
                                  &existing_off, &existing_len)) {
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }

        for (i = 0; i < statement->fields.count; ++i) {
                const struct analyzer_field* field =
                    &statement->fields.items[i];
                char type_name[DB_PAGE_SIZE];
                size_t type_length;
                size_t field_name_length;
                size_t field_length;
                size_t j;

                if (field->name == NULL || field->name[0] == '\0') {
                        return EXECUTOR_ERROR_BAD_STATEMENT;
                }

                type_length       = executor_type_name(&field->type, type_name,
                                                       sizeof(type_name));
                field_name_length = strlen(field->name);
                field_length =
                    sizeof(uint16_t) * 2 + type_length + field_name_length;
                if (type_length == 0 || type_length > UINT16_MAX ||
                    field_name_length > UINT16_MAX ||
                    field_length > DB_PAGE_SIZE - definition_length) {
                        return EXECUTOR_ERROR_BAD_STATEMENT;
                }

                for (j = 0; j < i; ++j) {
                        if (strcmp(field->name,
                                   statement->fields.items[j].name) == 0) {
                                return EXECUTOR_ERROR_BAD_STATEMENT;
                        }
                }

                definition_length +=
                    sizeof(uint16_t) * 2 + type_length + field_name_length;
        }

        if (!db_alloc_page(db, &definition_page)) {
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }

        definition = db->page_buf;
        put_u16_le(definition, (uint16_t)statement->fields.count);
        definition_length = sizeof(uint16_t);

        for (i = 0; i < statement->fields.count; ++i) {
                const struct analyzer_field* field =
                    &statement->fields.items[i];
                char type_name[DB_PAGE_SIZE];
                size_t type_length = executor_type_name(&field->type, type_name,
                                                        sizeof(type_name));
                size_t field_name_length = strlen(field->name);

                put_u16_le(definition + definition_length,
                           (uint16_t)type_length);
                definition_length += sizeof(uint16_t);
                memcpy(definition + definition_length, type_name, type_length);
                definition_length += type_length;
                put_u16_le(definition + definition_length,
                           (uint16_t)field_name_length);
                definition_length += sizeof(uint16_t);
                memcpy(definition + definition_length, field->name,
                       field_name_length);
                definition_length += field_name_length;
        }

        if (!db_write_page(db, definition_page, definition) ||
            !db_interface_dir_put(db, statement->name, definition_page, 0,
                                  (uint16_t)definition_length, false)) {
                db_free_page(db, definition_page);
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }

        db->hdr.iface_count++;
        database_mark_modified(db);
        return EXECUTOR_OK;
}
