/**
 * @file database.c
 * @brief Database handle, placeholder until the storage format exists.
 */

#include "interface/storage/database.h"

void database_init(struct database* db, const char* path, bool created) {
        db->path     = path;
        db->created  = created;
        db->modified = false;
}

void database_mark_modified(struct database* db) { db->modified = true; }

bool database_is_modified(const struct database* db) { return db->modified; }

bool database_save(struct database* db) {
        (void)db;
        return false;
}
