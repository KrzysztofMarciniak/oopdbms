/**
 * @file create_class.c
 * @brief CREATE CLASS <n> [EXTENDS <class>] [IMPLEMENTS <interface>[, ...]]
 * (<field> <type>[, ...])
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "implementation/executor/executor_internal.h"
#include "implementation/executor/helper/class_definition/main.h"
#include "interface/storage/page.h"

enum EXECUTOR_RESULT execute_create_class(
    struct database* db, const struct analyzer_statement* statement) {
        uint32_t existing_page;
        uint16_t existing_offset;
        uint16_t existing_length;
        helper_class_page_field* fields = NULL;
        char** type_names               = NULL;
        helper_class_page_definition definition;
        uint16_t definition_length;
        uint32_t definition_page = 0;
        bool page_allocated      = false;
        size_t i;
        enum EXECUTOR_RESULT result = EXECUTOR_ERROR_BAD_STATEMENT;

        if (db == NULL || statement == NULL || statement->name == NULL ||
            statement->name[0] == '\0' ||
            strlen(statement->name) > DB_PAGE_SIZE - DIR_PAGE_HDR_SIZE - 10u ||
            statement->fields.count == 0 ||
            statement->fields.count > UINT16_MAX ||
            statement->fields.items == NULL ||
            statement->implements.count > UINT16_MAX ||
            (statement->implements.count > 0 &&
             statement->implements.items == NULL) ||
            db->hdr.class_count == UINT32_MAX) {
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }

        if (db_class_dir_find(db, statement->name, &existing_page,
                              &existing_offset, &existing_length)) {
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }

        if (statement->parent != NULL &&
            (statement->parent[0] == '\0' ||
             !db_class_dir_find(db, statement->parent, &existing_page,
                                &existing_offset, &existing_length))) {
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }

        for (i = 0; i < statement->implements.count; ++i) {
                size_t j;

                if (statement->implements.items[i] == NULL ||
                    statement->implements.items[i][0] == '\0' ||
                    !db_interface_dir_find(db, statement->implements.items[i],
                                           &existing_page, &existing_offset,
                                           &existing_length)) {
                        return EXECUTOR_ERROR_BAD_STATEMENT;
                }
                for (j = 0; j < i; ++j) {
                        if (strcmp(statement->implements.items[i],
                                   statement->implements.items[j]) == 0) {
                                return EXECUTOR_ERROR_BAD_STATEMENT;
                        }
                }
        }

        fields     = calloc(statement->fields.count, sizeof(*fields));
        type_names = calloc(statement->fields.count, sizeof(*type_names));
        if (fields == NULL || type_names == NULL) {
                goto cleanup;
        }

        for (i = 0; i < statement->fields.count; ++i) {
                const struct analyzer_field* field =
                    &statement->fields.items[i];
                char type_buffer[DB_PAGE_SIZE];
                size_t type_length;
                size_t j;

                if (field->name == NULL || field->name[0] == '\0') {
                        goto cleanup;
                }
                for (j = 0; j < i; ++j) {
                        if (strcmp(field->name, fields[j].name) == 0) {
                                goto cleanup;
                        }
                }

                type_length = executor_type_name(&field->type, type_buffer,
                                                 sizeof(type_buffer));
                if (type_length == 0) {
                        goto cleanup;
                }
                if (field->type.kind == ANALYZER_TYPE_POINTER &&
                    !db_class_dir_find(db, field->type.class_name,
                                       &existing_page, &existing_offset,
                                       &existing_length)) {
                        goto cleanup;
                }

                type_names[i] = malloc(type_length + 1u);
                if (type_names[i] == NULL) {
                        goto cleanup;
                }
                memcpy(type_names[i], type_buffer, type_length + 1u);
                fields[i].type_name = type_names[i];
                fields[i].name      = field->name;
        }

        definition.field_count     = (uint16_t)statement->fields.count;
        definition.fields          = fields;
        definition.parent_name     = statement->parent;
        definition.interface_count = (uint16_t)statement->implements.count;
        definition.interfaces = (const char* const*)statement->implements.items;

        if (!db_alloc_page(db, &definition_page)) {
                goto cleanup;
        }
        page_allocated = true;

        if (helper_write_class_page_definition(
                db->page_buf, sizeof(db->page_buf), &definition,
                &definition_length) != 0 ||
            !db_write_page(db, definition_page, db->page_buf) ||
            !db_class_dir_put(db, statement->name, definition_page, 0,
                              definition_length, false)) {
                goto cleanup;
        }

        db->hdr.class_count++;
        database_mark_modified(db);
        result = EXECUTOR_OK;

cleanup:
        if (result != EXECUTOR_OK && page_allocated) {
                db_free_page(db, definition_page);
        }
        if (type_names != NULL) {
                for (i = 0; i < statement->fields.count; ++i) {
                        free(type_names[i]);
                }
        }
        free(type_names);
        free(fields);
        return result;
}
