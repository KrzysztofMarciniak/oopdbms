#include "interface/storage/database.h"

#include <stdlib.h>
#include <string.h>

static void put_u32_le(uint8_t* p, uint32_t v) {
        p[0] = (uint8_t)(v);
        p[1] = (uint8_t)(v >> 8);
        p[2] = (uint8_t)(v >> 16);
        p[3] = (uint8_t)(v >> 24);
}

static uint32_t get_u32_le(const uint8_t* p) {
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
               ((uint32_t)p[3] << 24);
}

static void header_to_bytes(const struct db_header* h, uint8_t* b) {
        memset(b, 0, STORAGE_HEADER_SIZE);
        memcpy(b + STORAGE_HDR_MAGIC_OFF, STORAGE_MAGIC, STORAGE_MAGIC_SIZE);
        put_u32_le(b + STORAGE_HDR_VERSION_OFF, STORAGE_VERSION);
        put_u32_le(b + STORAGE_HDR_PAGE_SIZE_OFF, h->page_size);
        put_u32_le(b + STORAGE_HDR_TOTAL_PAGES_OFF, h->total_pages);
        put_u32_le(b + STORAGE_HDR_FREE_LIST_HEAD_OFF, h->free_list_head);
        put_u32_le(b + STORAGE_HDR_CLASS_COUNT_OFF, h->class_count);
        put_u32_le(b + STORAGE_HDR_CLASS_DIR_PAGE_OFF, h->class_dir_page);
        put_u32_le(b + STORAGE_HDR_IFACE_COUNT_OFF, h->iface_count);
        put_u32_le(b + STORAGE_HDR_IFACE_DIR_PAGE_OFF, h->iface_dir_page);
        put_u32_le(b + STORAGE_HDR_FUNC_COUNT_OFF, h->func_count);
        put_u32_le(b + STORAGE_HDR_FUNC_DIR_PAGE_OFF, h->func_dir_page);
        put_u32_le(b + STORAGE_HDR_OBJ_COUNT_OFF, h->obj_count);
        put_u32_le(b + STORAGE_HDR_OBJ_DIR_PAGE_OFF, h->obj_dir_page);
}

static void bytes_to_header(const uint8_t* b, struct db_header* h) {
        h->version        = get_u32_le(b + STORAGE_HDR_VERSION_OFF);
        h->page_size      = get_u32_le(b + STORAGE_HDR_PAGE_SIZE_OFF);
        h->total_pages    = get_u32_le(b + STORAGE_HDR_TOTAL_PAGES_OFF);
        h->free_list_head = get_u32_le(b + STORAGE_HDR_FREE_LIST_HEAD_OFF);
        h->class_count    = get_u32_le(b + STORAGE_HDR_CLASS_COUNT_OFF);
        h->class_dir_page = get_u32_le(b + STORAGE_HDR_CLASS_DIR_PAGE_OFF);
        h->iface_count    = get_u32_le(b + STORAGE_HDR_IFACE_COUNT_OFF);
        h->iface_dir_page = get_u32_le(b + STORAGE_HDR_IFACE_DIR_PAGE_OFF);
        h->func_count     = get_u32_le(b + STORAGE_HDR_FUNC_COUNT_OFF);
        h->func_dir_page  = get_u32_le(b + STORAGE_HDR_FUNC_DIR_PAGE_OFF);
        h->obj_count      = get_u32_le(b + STORAGE_HDR_OBJ_COUNT_OFF);
        h->obj_dir_page   = get_u32_le(b + STORAGE_HDR_OBJ_DIR_PAGE_OFF);
}

void database_init(struct database* db, const char* path, bool created) {
        db->path      = path;
        db->created   = created;
        db->modified  = false;
        db->read_only = false;
        db->fp        = NULL;
        memset(&db->hdr, 0, sizeof db->hdr);
        if (created) {
                db->fp = fopen(path, "w+b");
                if (db->fp) {
                        db->hdr.version     = STORAGE_VERSION;
                        db->hdr.page_size   = DB_PAGE_SIZE;
                        db->hdr.total_pages = 1;
                        db_flush_header(db);
                }
        } else {
                db->fp = fopen(path, "r+b");
                if (db->fp) {
                        uint8_t buf[STORAGE_HEADER_SIZE];
                        if (fread(buf, 1, STORAGE_HEADER_SIZE, db->fp) ==
                            STORAGE_HEADER_SIZE) {
                                bytes_to_header(buf, &db->hdr);
                        }
                }
        }
}

void database_close(struct database* db) {
        if (db->fp) {
                if (db->modified) database_save(db);
                fclose(db->fp);
                db->fp = NULL;
        }
}

void database_mark_modified(struct database* db) { db->modified = true; }

bool database_is_modified(const struct database* db) { return db->modified; }

bool database_save(struct database* db) {
        if (!db->fp) return false;
        if (!db_flush_header(db)) return false;
        fflush(db->fp);
        db->modified = false;
        return true;
}

bool db_read_page(struct database* db, uint32_t page_no, void* dst) {
        if (!db->fp || page_no >= db->hdr.total_pages) return false;
        if (fseek(db->fp, (long)page_no * DB_PAGE_SIZE, SEEK_SET) != 0)
                return false;
        return fread(dst, 1, DB_PAGE_SIZE, db->fp) == DB_PAGE_SIZE;
}

bool db_write_page(struct database* db, uint32_t page_no, const void* src) {
        if (!db->fp || page_no >= db->hdr.total_pages) return false;
        if (fseek(db->fp, (long)page_no * DB_PAGE_SIZE, SEEK_SET) != 0)
                return false;
        if (fwrite(src, 1, DB_PAGE_SIZE, db->fp) != DB_PAGE_SIZE) return false;
        db->modified = true;
        return true;
}

bool db_flush_header(struct database* db) {
        if (!db->fp) return false;
        uint8_t buf[STORAGE_HEADER_SIZE];
        header_to_bytes(&db->hdr, buf);
        if (fseek(db->fp, 0, SEEK_SET) != 0) return false;
        if (fwrite(buf, 1, STORAGE_HEADER_SIZE, db->fp) != STORAGE_HEADER_SIZE)
                return false;
        /* pad rest of page 0? leave */
        fflush(db->fp);
        return true;
}

bool db_alloc_page(struct database* db, uint32_t* out_page_no) {
        if (!db->fp || !out_page_no) return false;
        uint32_t pg;
        if (db->hdr.free_list_head != 0) {
                pg = db->hdr.free_list_head;
                uint8_t buf[DB_PAGE_SIZE];
                if (!db_read_page(db, pg, buf)) return false;
                db->hdr.free_list_head = get_u32_le(buf + FREE_PAGE_NEXT_OFF);
        } else {
                pg = db->hdr.total_pages++;
                memset(db->page_buf, 0, DB_PAGE_SIZE);
                if (!db_write_page(db, pg, db->page_buf)) return false;
        }
        /* zero page */
        memset(db->page_buf, 0, DB_PAGE_SIZE);
        db_write_page(db, pg, db->page_buf);
        *out_page_no = pg;
        db->modified = true;
        db_flush_header(db);
        return true;
}

bool db_free_page(struct database* db, uint32_t page_no) {
        if (!db->fp || page_no == 0) return false;
        uint8_t buf[DB_PAGE_SIZE];
        memset(buf, 0, DB_PAGE_SIZE);
        put_u32_le(buf + FREE_PAGE_NEXT_OFF, db->hdr.free_list_head);
        if (!db_write_page(db, page_no, buf)) return false;
        db->hdr.free_list_head = page_no;
        db->modified           = true;
        db_flush_header(db);
        return true;
}

/* Directory stubs */
bool db_class_dir_find(struct database* db, const char* name,
                       uint32_t* out_page, uint16_t* out_off,
                       uint16_t* out_len) {
        (void)db;
        (void)name;
        (void)out_page;
        (void)out_off;
        (void)out_len;
        return false;
}

bool db_class_dir_put(struct database* db, const char* name, uint32_t loc_page,
                      uint16_t loc_off, uint16_t loc_len, bool overwrite) {
        (void)db;
        (void)name;
        (void)loc_page;
        (void)loc_off;
        (void)loc_len;
        (void)overwrite;
        return false;
}

bool db_class_dir_remove(struct database* db, const char* name) {
        (void)db;
        (void)name;
        return false;
}

bool db_interface_dir_find(struct database* db, const char* name,
                           uint32_t* out_page, uint16_t* out_off,
                           uint16_t* out_len) {
        (void)db;
        (void)name;
        (void)out_page;
        (void)out_off;
        (void)out_len;
        return false;
}

bool db_interface_dir_put(struct database* db, const char* name,
                          uint32_t loc_page, uint16_t loc_off, uint16_t loc_len,
                          bool overwrite) {
        (void)db;
        (void)name;
        (void)loc_page;
        (void)loc_off;
        (void)loc_len;
        (void)overwrite;
        return false;
}

bool db_interface_dir_remove(struct database* db, const char* name) {
        (void)db;
        (void)name;
        return false;
}

bool db_function_dir_find(struct database* db, const char* class_name,
                          const char* func_name, uint32_t* out_page,
                          uint16_t* out_off, uint16_t* out_len) {
        (void)db;
        (void)class_name;
        (void)func_name;
        (void)out_page;
        (void)out_off;
        (void)out_len;
        return false;
}

bool db_function_dir_put(struct database* db, const char* class_name,
                         const char* func_name, uint32_t loc_page,
                         uint16_t loc_off, uint16_t loc_len, bool overwrite) {
        (void)db;
        (void)class_name;
        (void)func_name;
        (void)loc_page;
        (void)loc_off;
        (void)loc_len;
        (void)overwrite;
        return false;
}

bool db_function_dir_remove(struct database* db, const char* class_name,
                            const char* func_name) {
        (void)db;
        (void)class_name;
        (void)func_name;
        return false;
}

bool db_dir_ensure_chain(struct database* db, uint32_t* first_page_out) {
        (void)db;
        (void)first_page_out;
        return false;
}

bool db_dir_get_first_page(const struct database* db, int kind, uint32_t* out) {
        (void)db;
        (void)kind;
        (void)out;
        return false;
}

bool db_dir_set_first_page(struct database* db, int kind, uint32_t first_page) {
        (void)db;
        (void)kind;
        (void)first_page;
        return false;
}
