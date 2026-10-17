/**
 * @file create_class.c
 * @brief CREATE CLASS <n> [EXTENDS <class>] [IMPLEMENTS <interface>[, ...]]
 * (<field> <type>[, ...])
 */

#include <stdlib.h>
#include <string.h>

#include "implementation/executor/executor_internal.h"
#include "implementation/executor/helper/class_definition/main.h"
#include "interface/storage/database.h"
#include "interface/storage/page.h"

/* put_u16_le and put_u32_le are now defined in database.c */

enum EXECUTOR_RESULT execute_create_class(
    struct database* db, const struct analyzer_statement* statement) {
        if (!db || !statement) {
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }

        /* Check if class already exists */
        uint32_t existing_page;
        uint16_t existing_off;
        uint16_t existing_len;
        if (db_class_dir_find(db, statement->name, &existing_page,
                              &existing_off, &existing_len)) {
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }

        /* Allocate storage for the definition blob */
        uint32_t def_page;
        if (!db_alloc_page(db, &def_page)) {
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }

        /* Create the definition blob */
        uint8_t* def_blob     = db->page_buf;
        uint32_t def_blob_len = 0;

        /* Add field count (u16) */
        uint16_t field_count = (uint16_t)statement->fields.count;
        put_u16_le(def_blob + def_blob_len, field_count);
        def_blob_len += 2;

        /* Add each field's type and name */
        for (size_t i = 0; i < statement->fields.count; i++) {
                struct analyzer_field* field = statement->fields.items + i;

                /* type_len (u16) + type + name_len (u16) + name */
                uint16_t type_len = (uint16_t)strlen(field->type.class_name);
                put_u16_le(def_blob + def_blob_len, type_len);
                def_blob_len += 2;

                memcpy(def_blob + def_blob_len, field->type.class_name,
                       type_len);
                def_blob_len += type_len;

                uint16_t name_len = (uint16_t)strlen(field->name);
                put_u16_le(def_blob + def_blob_len, name_len);
                def_blob_len += 2;

                memcpy(def_blob + def_blob_len, field->name, name_len);
                def_blob_len += name_len;
        }

        /* Add parent name if specified */
        if (statement->parent) {
                uint16_t parent_len = (uint16_t)strlen(statement->parent);
                put_u16_le(def_blob + def_blob_len, parent_len);
                def_blob_len += 2;
                memcpy(def_blob + def_blob_len, statement->parent, parent_len);
                def_blob_len += parent_len;
        } else {
                put_u16_le(def_blob + def_blob_len, 0);
                def_blob_len += 2;
        }

        /* Add each implemented interface */
        if (statement->implements.count > 0) {
                for (size_t i = 0; i < statement->implements.count; i++) {
                        char* iface        = statement->implements.items[i];
                        uint16_t iface_len = (uint16_t)strlen(iface);
                        put_u16_le(def_blob + def_blob_len, iface_len);
                        def_blob_len += 2;
                        memcpy(def_blob + def_blob_len, iface, iface_len);
                        def_blob_len += iface_len;
                }
        } else {
                put_u16_le(def_blob + def_blob_len, 0);
                def_blob_len += 2;
        }

        /* Write the definition blob to its page */
        if (!db_write_page(db, def_page, def_blob)) {
                db_free_page(db, def_page);
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }

        /* Update the header class count */
        db->hdr.class_count++;

        /* Add to the class directory */
        uint16_t name_len    = (uint16_t)strlen(statement->name);
        uint32_t parent_page = 0;
        uint16_t parent_off  = 0;
        uint16_t parent_len  = 0;
        if (statement->parent) {
                /* Find parent class location */
                if (db_class_dir_find(db, statement->parent, &parent_page,
                                      &parent_off, &parent_len)) {
                        /* Remove allocated page on error */
                        db_free_page(db, def_page);
                        return EXECUTOR_ERROR_BAD_STATEMENT;
                }
        }

        uint16_t impl_count = (uint16_t)statement->implements.count;
        uint32_t obj_page   = 0;

        /* Write class directory entry */
        put_u16_le(def_blob, name_len);
        put_u32_le(def_blob + 2, def_page);
        put_u32_le(def_blob + 6, def_blob_len);
        put_u16_le(def_blob + 10, parent_len);
        put_u16_le(def_blob + 12, impl_count);
        put_u32_le(def_blob + 14, obj_page);
        put_u16_le(def_blob + 18, (uint16_t)statement->fields.count);

        if (!db_class_dir_put(db, statement->name, def_page, 0, def_blob_len,
                              true)) {
                db_free_page(db, def_page);
                return EXECUTOR_ERROR_BAD_STATEMENT;
        }

        database_mark_modified(db);
        return EXECUTOR_OK;
}
