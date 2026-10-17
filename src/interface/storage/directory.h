#ifndef INTERFACE_STORAGE_DIRECTORY_H
#define INTERFACE_STORAGE_DIRECTORY_H

#include <stdbool.h>
#include <stdint.h>

#include "interface/storage/database.h"

/**
 * @file directory.h
 * @brief Directory API for schema elements (classes, interfaces, functions)
 */

/**
 * @brief A directory entry (name + location in storage)
 */
struct dir_entry {
        /** Name of the schema element */
        const char* name;
        /** Name length in bytes; the name is not NUL-terminated */
        uint16_t name_length;
        /** Page where definition blob lives */
        uint32_t loc_page;
        /** Offset within the page */
        uint16_t loc_off;
        /** Length of definition blob */
        uint16_t loc_len;
};

/** Visit callback for one directory entry; entry data is borrowed. */
typedef void (*db_dir_visit_callback)(const struct dir_entry* entry,
                                      void* context);

/**
 * @brief Create a new directory page chain
 * @param db Database handle
 * @param[out] first_page Output page number for new directory
 * @return true on success, false on failure
 */
bool db_dir_ensure_chain(struct database* db, uint32_t* first_page_out);

/**
 * @brief Find a directory entry by name in a specific directory
 * @param db Database handle
 * @param kind Directory kind (CLASS_DIR, IFACE_DIR, FUNC_DIR)
 * @param name Name to search for (class_name.func_name for functions)
 * @param[out] entry Output directory entry if found
 * @return true if found, false otherwise
 */
bool db_dir_find(struct database* db, int kind, const char* name,
                 struct dir_entry* entry);

/**
 * @brief Visit every entry in a directory page chain.
 * @param db Database handle
 * @param kind Directory kind (CLASS_DIR, IFACE_DIR, FUNC_DIR)
 * @param callback Function called once for each entry
 * @param context Caller-provided callback context
 * @return true on success, false if the directory cannot be read or is invalid
 */
bool db_dir_visit(struct database* db, int kind, db_dir_visit_callback callback,
                  void* context);

/**
 * @brief Put a directory entry into a specific directory
 * @param db Database handle
 * @param kind Directory kind (CLASS_DIR, IFACE_DIR, FUNC_DIR)
 * @param name Name of entry
 * @param loc_page Page where definition blob lives
 * @param loc_off Offset within the page
 * @param loc_len Length of definition blob
 * @param overwrite If true, replace existing entry with same name
 * @return true on success, false on failure
 */
bool db_dir_put(struct database* db, int kind, const char* name,
                uint32_t loc_page, uint16_t loc_off, uint16_t loc_len,
                bool overwrite);

/**
 * @brief Remove a directory entry by name
 * @param db Database handle
 * @param kind Directory kind (CLASS_DIR, IFACE_DIR, FUNC_DIR)
 * @param name Name to remove
 * @return true on success, false if not found
 */
bool db_dir_remove(struct database* db, int kind, const char* name);

/**
 * @brief Get the first page number for a directory kind
 * @param db Database handle
 * @param kind Directory kind (CLASS_DIR, IFACE_DIR, FUNC_DIR)
 * @param[out] out_page Output page number
 * @return true if directory exists, false otherwise
 */
bool db_dir_get_first_page(const struct database* db, int kind, uint32_t* out);

/**
 * @brief Set the first page number for a directory kind
 * @param db Database handle
 * @param kind Directory kind (CLASS_DIR, IFACE_DIR, FUNC_DIR)
 * @param first_page Page number to set
 * @return true on success, false on failure
 */
bool db_dir_set_first_page(struct database* db, int kind, uint32_t first_page);

/**
 * @brief Get the directory kind constant for class directory
 */
#define CLASS_DIR 0

/**
 * @brief Get the directory kind constant for interface directory
 */
#define IFACE_DIR 1

/**
 * @brief Get the directory kind constant for function directory
 */
#define FUNC_DIR 2

#endif /* INTERFACE_STORAGE_DIRECTORY_H */
