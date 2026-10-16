#ifndef INTERFACE_STORAGE_DATABASE_H
#define INTERFACE_STORAGE_DATABASE_H

/**
 * @file database.h
 * @brief The database handle the executor works on.
 *
 * The file format is not written yet: for now the handle only knows which
 * file it stands for and whether a command changed something.
 */

#include <stdbool.h>

/**
 * @brief An open database.
 */
struct database {
        /** Path of the file checked in step 3, borrowed from the caller. */
        const char* path;
        /** True if the file was created by this run. */
        bool created;
        /** True if a command changed data that still has to be saved. */
        bool modified;
};

/**
 * @brief Set up a handle for a checked file.
 *
 * @param db      Handle to set up.
 * @param path    Path of the database file.
 * @param created True if step 3 just created the file.
 */
void database_init(struct database* db, const char* path, bool created);

/**
 * @brief Remember that data changed and must be saved.
 *
 * @param db Handle.
 */
void database_mark_modified(struct database* db);

/**
 * @brief Whether there are unsaved changes.
 *
 * @param db Handle.
 * @return True if a command marked the database modified.
 */
bool database_is_modified(const struct database* db);

/**
 * @brief Write unsaved changes to the file.
 *
 * Not written yet: it always fails, so a command that starts changing data
 * before the storage format exists reports an error and does not pretend to
 * have saved.
 *
 * @param db Handle.
 * @return True if the changes are on disk.
 */
bool database_save(struct database* db);

#endif// INTERFACE_STORAGE_DATABASE_H
