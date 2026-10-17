#ifndef INTERFACE_STORAGE_DATABASE_H
#define INTERFACE_STORAGE_DATABASE_H

/**
 * @file database.h
 * @brief The database handle the executor works on (page-based v2).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "interface/storage/header.h"
#include "interface/storage/page.h"

/**
 * @brief Little-endian byte manipulation functions
 */
void put_u16_le(uint8_t* p, uint16_t v);
void put_u32_le(uint8_t* p, uint32_t v);
uint16_t get_u16_le(const uint8_t* p);
uint32_t get_u32_le(const uint8_t* p);

/**
 * @brief Cached database header (Page 0, v2), all values in host order after
 * load.
 */
struct db_header {
        uint32_t version;
        uint32_t page_size;
        uint32_t total_pages;
        uint32_t free_list_head; /* 0 = none */
        uint32_t class_count;
        uint32_t class_dir_page; /* 0 = none */
        uint32_t iface_count;
        uint32_t iface_dir_page; /* 0 = none */
        uint32_t func_count;
        uint32_t func_dir_page; /* 0 = none */
        uint32_t obj_count;     /* global (optional) */
        uint32_t obj_dir_page;  /* reserved */
};

/**
 * @brief An open database.
 */
struct database {
        /** Path of the file checked in step 3, borrowed from the caller. */
        const char* path;
        /** True if the file was created by this run (step 3 created it). */
        bool created;
        /** True if a command changed data that still has to be saved. */
        bool modified;
        /** True if opened read-only (defensive). */
        bool read_only;
        /** Open file handle (r+b or w+b). Owned by this handle while open. */
        FILE* fp;
        /** Cached header loaded from/written to page 0. */
        struct db_header hdr;
        /** Scratch page buffer for I/O (size DB_PAGE_SIZE). */
        uint8_t page_buf[DB_PAGE_SIZE];
};

/* Lifecycle */
void database_init(struct database* db, const char* path, bool created);
void database_close(struct database* db);
void database_mark_modified(struct database* db);
bool database_is_modified(const struct database* db);
bool database_save(struct database* db);

/* Page I/O */
bool db_read_page(struct database* db, uint32_t page_no, void* dst);
bool db_write_page(struct database* db, uint32_t page_no, const void* src);
bool db_flush_header(struct database* db);

/* Page allocation/free list */
bool db_alloc_page(struct database* db, uint32_t* out_page_no);
bool db_free_page(struct database* db, uint32_t page_no);

/* Directory: classes */
bool db_class_dir_find(struct database* db, const char* name,
                       uint32_t* out_page, uint16_t* out_off,
                       uint16_t* out_len);
bool db_class_dir_put(struct database* db, const char* name, uint32_t loc_page,
                      uint16_t loc_off, uint16_t loc_len, bool overwrite);
bool db_class_dir_remove(struct database* db, const char* name);

/* Directory: interfaces */
bool db_interface_dir_find(struct database* db, const char* name,
                           uint32_t* out_page, uint16_t* out_off,
                           uint16_t* out_len);
bool db_interface_dir_put(struct database* db, const char* name,
                          uint32_t loc_page, uint16_t loc_off, uint16_t loc_len,
                          bool overwrite);
bool db_interface_dir_remove(struct database* db, const char* name);

/* Directory: functions (class.func) */
bool db_function_dir_find(struct database* db, const char* class_name,
                          const char* func_name, uint32_t* out_page,
                          uint16_t* out_off, uint16_t* out_len);
bool db_function_dir_put(struct database* db, const char* class_name,
                         const char* func_name, uint32_t loc_page,
                         uint16_t loc_off, uint16_t loc_len, bool overwrite);
bool db_function_dir_remove(struct database* db, const char* class_name,
                            const char* func_name);

/* Directory: raw helpers */
bool db_dir_ensure_chain(struct database* db, uint32_t* first_page_out);
bool db_dir_get_first_page(const struct database* db, int kind, uint32_t* out);
bool db_dir_set_first_page(struct database* db, int kind, uint32_t first_page);

#endif /* INTERFACE_STORAGE_DATABASE_H */
