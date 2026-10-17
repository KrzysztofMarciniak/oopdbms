/**
 * @file create_interface.c
 * @brief CREATE INTERFACE <n> (<field> <type>[, ...])
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "implementation/executor/executor_internal.h"
#include "implementation/executor/helper/interface_definition/main.h"
#include "interface/storage/page.h"

enum EXECUTOR_RESULT execute_create_interface(
    struct database* db, const struct analyzer_statement* statement) {
        size_t i;
        uint32_t existing_page;
        uint16_t existing_off;
        uint16_t existing_len;
        uint32_t definition_page            = 0;
        bool page_allocated                 = false;
        helper_interface_page_field* fields = NULL;
        char* type_names                    = NULL;
        helper_interface_page_definition definition;
        uint16_t definition_length;
        enum EXECUTOR_RESULT result = EXECUTOR_ERROR_BAD_STATEMENT;

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

        do {
                bool fields_valid = true;

                if (statement->fields.count > 0) {
                        fields =
                            calloc(statement->fields.count, sizeof(*fields));
                        type_names =
                            calloc(statement->fields.count, DB_PAGE_SIZE);
                        if (fields == NULL || type_names == NULL) {
                                break;
                        }
                }

                for (i = 0; i < statement->fields.count; ++i) {
                        const struct analyzer_field* field =
                            &statement->fields.items[i];
                        size_t type_length;
                        size_t j;

                        if (field->name == NULL || field->name[0] == '\0') {
                                fields_valid = false;
                                break;
                        }

                        fields[i].type_name = type_names + i * DB_PAGE_SIZE;
                        fields[i].name      = field->name;
                        type_length         = executor_type_name(
                            &field->type, (char*)fields[i].type_name,
                            DB_PAGE_SIZE);
                        if (type_length == 0) {
                                fields_valid = false;
                                break;
                        }

                        for (j = 0; j < i; ++j) {
                                if (strcmp(field->name,
                                           statement->fields.items[j].name) ==
                                    0) {
                                        fields_valid = false;
                                        break;
                                }
                        }
                        if (!fields_valid) {
                                break;
                        }
                }

                if (!fields_valid) {
                        break;
                }

                if (!db_alloc_page(db, &definition_page)) {
                        break;
                }
                page_allocated = true;

                definition.field_count = (uint16_t)statement->fields.count;
                definition.fields      = fields;
                if (helper_write_interface_page_definition(
                        db->page_buf, sizeof(db->page_buf), &definition,
                        &definition_length) != 0) {
                        break;
                }

                if (!db_write_page(db, definition_page, db->page_buf) ||
                    !db_interface_dir_put(db, statement->name, definition_page,
                                          0, definition_length, false)) {
                        break;
                }

                db->hdr.iface_count++;
                database_mark_modified(db);
                result = EXECUTOR_OK;
        } while (0);

        if (result != EXECUTOR_OK && page_allocated) {
                db_free_page(db, definition_page);
        }

        free(type_names);
        free(fields);
        return result;
}
