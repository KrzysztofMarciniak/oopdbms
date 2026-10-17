#include "interface/storage/database.h"

#include <stdlib.h>
#include <string.h>

#include "implementation/executor/helper/binary_encoding.h"
#include "implementation/executor/helper/create_header.h"
#include "implementation/executor/helper/create_metatable.h"
#include "implementation/executor/helper/format_types.h"
#include "implementation/executor/helper/validate_metadata_layout.h"
#include "interface/storage/directory.h"

#define DIR_ENTRY_FIXED_SIZE 10U

static bool dir_kind_page(const struct database* db, int kind,
                          uint32_t* const page) {
        if (db == NULL || page == NULL) {
                return false;
        }

        switch (kind) {
                case CLASS_DIR:
                        *page = db->hdr.class_dir_page;
                        return true;
                case IFACE_DIR:
                        *page = db->hdr.iface_dir_page;
                        return true;
                case FUNC_DIR:
                        *page = db->hdr.func_dir_page;
                        return true;
                default:
                        return false;
        }
}

static bool dir_entry_at(const uint8_t* page, uint16_t free_off,
                         uint16_t offset, uint16_t* next_offset,
                         struct dir_entry* entry) {
        uint16_t name_len;
        uint16_t entry_len;

        if (page == NULL || offset > free_off ||
            (uint16_t)(free_off - offset) < (uint16_t)DIR_ENTRY_FIXED_SIZE) {
                return false;
        }

        name_len = get_u16_le(page + offset);
        if (name_len > UINT16_MAX - DIR_ENTRY_FIXED_SIZE) {
                return false;
        }
        entry_len = (uint16_t)(DIR_ENTRY_FIXED_SIZE + name_len);
        if (entry_len > free_off - offset) {
                return false;
        }

        if (entry != NULL) {
                entry->name =
                    (const char*)(page + offset + DIR_ENTRY_FIXED_SIZE);
                entry->name_length = name_len;
                entry->loc_page    = get_u32_le(page + offset + 2);
                entry->loc_off     = get_u16_le(page + offset + 6);
                entry->loc_len     = get_u16_le(page + offset + 8);
        }
        if (next_offset != NULL) {
                *next_offset = (uint16_t)(offset + entry_len);
        }
        return true;
}

static bool dir_entry_matches(const uint8_t* page, uint16_t offset,
                              const char* name) {
        size_t name_len;

        if (page == NULL || name == NULL) {
                return false;
        }

        name_len = strlen(name);
        return name_len == get_u16_le(page + offset) &&
               memcmp(page + offset + DIR_ENTRY_FIXED_SIZE, name, name_len) ==
                   0;
}

static void dir_init_page(uint8_t* page) {
        memset(page, 0, DB_PAGE_SIZE);
        put_u16_le(page + DIR_PAGE_COUNT_OFF, 0);
        put_u16_le(page + DIR_PAGE_FREE_OFF_OFF, DIR_PAGE_HDR_SIZE);
        put_u32_le(page + DIR_PAGE_NEXT_OFF, 0);
}

bool db_dir_ensure_chain(struct database* db, uint32_t* first_page_out) {
        uint32_t page_no;
        uint8_t page[DB_PAGE_SIZE];

        if (db == NULL || first_page_out == NULL ||
            !db_alloc_page(db, &page_no)) {
                return false;
        }

        dir_init_page(page);
        if (!db_write_page(db, page_no, page)) {
                db_free_page(db, page_no);
                return false;
        }

        *first_page_out = page_no;
        return true;
}

bool db_dir_get_first_page(const struct database* db, int kind,
                           uint32_t* out_page) {
        return dir_kind_page(db, kind, out_page) && *out_page != 0;
}

bool db_dir_set_first_page(struct database* db, int kind, uint32_t first_page) {
        if (db == NULL) {
                return false;
        }

        switch (kind) {
                case CLASS_DIR:
                        db->hdr.class_dir_page = first_page;
                        break;
                case IFACE_DIR:
                        db->hdr.iface_dir_page = first_page;
                        break;
                case FUNC_DIR:
                        db->hdr.func_dir_page = first_page;
                        break;
                default:
                        return false;
        }

        database_mark_modified(db);
        return true;
}

bool db_dir_find(struct database* db, int kind, const char* name,
                 struct dir_entry* entry) {
        uint32_t page_no;

        if (db == NULL || name == NULL ||
            !db_dir_get_first_page(db, kind, &page_no)) {
                return false;
        }

        while (page_no != 0) {
                uint16_t count;
                uint16_t free_off;
                uint16_t offset = DIR_PAGE_HDR_SIZE;
                size_t i;

                if (!db_read_page(db, page_no, db->page_buf)) {
                        return false;
                }

                count    = get_u16_le(db->page_buf + DIR_PAGE_COUNT_OFF);
                free_off = get_u16_le(db->page_buf + DIR_PAGE_FREE_OFF_OFF);
                if (free_off < DIR_PAGE_HDR_SIZE || free_off > DB_PAGE_SIZE) {
                        return false;
                }

                for (i = 0; i < count; ++i) {
                        uint16_t next_offset;

                        if (!dir_entry_at(db->page_buf, free_off, offset,
                                          &next_offset, entry)) {
                                return false;
                        }
                        if (dir_entry_matches(db->page_buf, offset, name)) {
                                return true;
                        }
                        offset = next_offset;
                }
                page_no = get_u32_le(db->page_buf + DIR_PAGE_NEXT_OFF);
        }

        return false;
}

bool db_dir_visit(struct database* db, int kind, db_dir_visit_callback callback,
                  void* context) {
        uint32_t page_no;
        uint32_t pages_visited = 0;
        uint8_t page[DB_PAGE_SIZE];

        if (db == NULL || callback == NULL ||
            !db_dir_get_first_page(db, kind, &page_no)) {
                return false;
        }

        while (page_no != 0) {
                uint16_t count;
                uint16_t free_off;
                uint16_t offset = DIR_PAGE_HDR_SIZE;
                size_t i;

                if (pages_visited >= db->hdr.total_pages ||
                    !db_read_page(db, page_no, page)) {
                        return false;
                }
                pages_visited++;

                count    = get_u16_le(page + DIR_PAGE_COUNT_OFF);
                free_off = get_u16_le(page + DIR_PAGE_FREE_OFF_OFF);
                if (free_off < DIR_PAGE_HDR_SIZE || free_off > DB_PAGE_SIZE) {
                        return false;
                }

                for (i = 0; i < count; ++i) {
                        struct dir_entry entry;
                        uint16_t next_offset;

                        if (!dir_entry_at(page, free_off, offset, &next_offset,
                                          &entry)) {
                                return false;
                        }
                        callback(&entry, context);
                        offset = next_offset;
                }
                page_no = get_u32_le(page + DIR_PAGE_NEXT_OFF);
        }

        return true;
}

bool db_dir_put(struct database* db, int kind, const char* name,
                uint32_t loc_page, uint16_t loc_off, uint16_t loc_len,
                bool overwrite) {
        uint32_t page_no;
        size_t name_len;
        uint16_t entry_len;

        if (db == NULL || name == NULL || name[0] == '\0') {
                return false;
        }

        name_len = strlen(name);
        if (name_len > UINT16_MAX || name_len > DB_PAGE_SIZE -
                                                    DIR_PAGE_HDR_SIZE -
                                                    DIR_ENTRY_FIXED_SIZE) {
                return false;
        }
        entry_len = (uint16_t)(DIR_ENTRY_FIXED_SIZE + name_len);

        if (!db_dir_get_first_page(db, kind, &page_no)) {
                if (!db_dir_ensure_chain(db, &page_no) ||
                    !db_dir_set_first_page(db, kind, page_no)) {
                        return false;
                }
        }

        for (;;) {
                uint16_t count;
                uint16_t free_off;
                uint16_t offset = DIR_PAGE_HDR_SIZE;
                uint32_t next_page;
                size_t i;

                if (!db_read_page(db, page_no, db->page_buf)) {
                        return false;
                }

                count     = get_u16_le(db->page_buf + DIR_PAGE_COUNT_OFF);
                free_off  = get_u16_le(db->page_buf + DIR_PAGE_FREE_OFF_OFF);
                next_page = get_u32_le(db->page_buf + DIR_PAGE_NEXT_OFF);
                if (free_off < DIR_PAGE_HDR_SIZE || free_off > DB_PAGE_SIZE) {
                        return false;
                }

                for (i = 0; i < count; ++i) {
                        uint16_t next_offset;

                        if (!dir_entry_at(db->page_buf, free_off, offset,
                                          &next_offset, NULL)) {
                                return false;
                        }
                        if (dir_entry_matches(db->page_buf, offset, name)) {
                                if (!overwrite) {
                                        return false;
                                }
                                put_u32_le(db->page_buf + offset + 2, loc_page);
                                put_u16_le(db->page_buf + offset + 6, loc_off);
                                put_u16_le(db->page_buf + offset + 8, loc_len);
                                return db_write_page(db, page_no, db->page_buf);
                        }
                        offset = next_offset;
                }

                if (entry_len <= DB_PAGE_SIZE - free_off) {
                        put_u16_le(db->page_buf + free_off, (uint16_t)name_len);
                        put_u32_le(db->page_buf + free_off + 2, loc_page);
                        put_u16_le(db->page_buf + free_off + 6, loc_off);
                        put_u16_le(db->page_buf + free_off + 8, loc_len);
                        memcpy(db->page_buf + free_off + DIR_ENTRY_FIXED_SIZE,
                               name, name_len);
                        put_u16_le(db->page_buf + DIR_PAGE_COUNT_OFF,
                                   (uint16_t)(count + 1));
                        put_u16_le(db->page_buf + DIR_PAGE_FREE_OFF_OFF,
                                   (uint16_t)(free_off + entry_len));
                        return db_write_page(db, page_no, db->page_buf);
                }

                if (next_page == 0) {
                        if (!db_dir_ensure_chain(db, &next_page)) {
                                return false;
                        }
                        put_u32_le(db->page_buf + DIR_PAGE_NEXT_OFF, next_page);
                        if (!db_write_page(db, page_no, db->page_buf)) {
                                return false;
                        }
                }
                page_no = next_page;
        }
}

bool db_dir_remove(struct database* db, int kind, const char* name) {
        uint32_t page_no;

        if (db == NULL || name == NULL ||
            !db_dir_get_first_page(db, kind, &page_no)) {
                return false;
        }

        while (page_no != 0) {
                uint16_t count;
                uint16_t free_off;
                uint16_t offset = DIR_PAGE_HDR_SIZE;
                size_t i;

                if (!db_read_page(db, page_no, db->page_buf)) {
                        return false;
                }

                count    = get_u16_le(db->page_buf + DIR_PAGE_COUNT_OFF);
                free_off = get_u16_le(db->page_buf + DIR_PAGE_FREE_OFF_OFF);
                if (free_off < DIR_PAGE_HDR_SIZE || free_off > DB_PAGE_SIZE) {
                        return false;
                }

                for (i = 0; i < count; ++i) {
                        uint16_t next_offset;

                        if (!dir_entry_at(db->page_buf, free_off, offset,
                                          &next_offset, NULL)) {
                                return false;
                        }
                        if (dir_entry_matches(db->page_buf, offset, name)) {
                                size_t remaining = free_off - next_offset;

                                memmove(db->page_buf + offset,
                                        db->page_buf + next_offset, remaining);
                                memset(db->page_buf + free_off -
                                           (next_offset - offset),
                                       0, next_offset - offset);
                                put_u16_le(db->page_buf + DIR_PAGE_COUNT_OFF,
                                           (uint16_t)(count - 1));
                                put_u16_le(db->page_buf + DIR_PAGE_FREE_OFF_OFF,
                                           (uint16_t)(free_off -
                                                      (next_offset - offset)));
                                return db_write_page(db, page_no, db->page_buf);
                        }
                        offset = next_offset;
                }
                page_no = get_u32_le(db->page_buf + DIR_PAGE_NEXT_OFF);
        }

        return false;
}

bool db_class_dir_find(struct database* db, const char* name,
                       uint32_t* out_page, uint16_t* out_off,
                       uint16_t* out_len) {
        struct dir_entry entry;

        if (out_page == NULL || out_off == NULL || out_len == NULL ||
            !db_dir_find(db, CLASS_DIR, name, &entry)) {
                return false;
        }
        *out_page = entry.loc_page;
        *out_off  = entry.loc_off;
        *out_len  = entry.loc_len;
        return true;
}

bool db_class_dir_put(struct database* db, const char* name, uint32_t loc_page,
                      uint16_t loc_off, uint16_t loc_len, bool overwrite) {
        return db_dir_put(db, CLASS_DIR, name, loc_page, loc_off, loc_len,
                          overwrite);
}

bool db_class_dir_remove(struct database* db, const char* name) {
        return db_dir_remove(db, CLASS_DIR, name);
}

bool db_interface_dir_find(struct database* db, const char* name,
                           uint32_t* out_page, uint16_t* out_off,
                           uint16_t* out_len) {
        struct dir_entry entry;

        if (out_page == NULL || out_off == NULL || out_len == NULL ||
            !db_dir_find(db, IFACE_DIR, name, &entry)) {
                return false;
        }
        *out_page = entry.loc_page;
        *out_off  = entry.loc_off;
        *out_len  = entry.loc_len;
        return true;
}

bool db_interface_dir_put(struct database* db, const char* name,
                          uint32_t loc_page, uint16_t loc_off, uint16_t loc_len,
                          bool overwrite) {
        return db_dir_put(db, IFACE_DIR, name, loc_page, loc_off, loc_len,
                          overwrite);
}

bool db_interface_dir_remove(struct database* db, const char* name) {
        return db_dir_remove(db, IFACE_DIR, name);
}

void put_u16_le(uint8_t* p, uint16_t v) {
        p[0] = (uint8_t)(v);
        p[1] = (uint8_t)(v >> 8);
}

void put_u32_le(uint8_t* p, uint32_t v) {
        p[0] = (uint8_t)(v);
        p[1] = (uint8_t)(v >> 8);
        p[2] = (uint8_t)(v >> 16);
        p[3] = (uint8_t)(v >> 24);
}

uint16_t get_u16_le(const uint8_t* p) {
        return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

uint32_t get_u32_le(const uint8_t* p) {
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
        db->path        = path;
        db->created     = created;
        db->modified    = false;
        db->read_only   = false;
        db->format_mode = false;
        db->fp          = NULL;
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

bool database_init_format_mode(struct database* db, const char* path,
                               bool created, bool read_only) {
        uint8_t header_bytes[EXECUTOR_FORMAT_HEADER_SIZE];
        uint8_t metatable_bytes[EXECUTOR_FORMAT_METATABLE_SIZE];
        struct executor_format_header header       = {0};
        struct executor_format_metatable metatable = {0};
        long file_size;

        if (db == NULL || path == NULL || (created && read_only)) {
                return false;
        }
        memset(db, 0, sizeof *db);
        db->path    = path;
        db->created = created;
        db->fp      = fopen(path, created ? "w+b" : read_only ? "rb" : "r+b");
        if (db->fp == NULL) {
                return false;
        }
        db->format_mode = true;
        db->read_only   = read_only;

        if (created) {
                if (!executor_helper_create_header(
                        header_bytes, sizeof header_bytes, &header) ||
                    !executor_helper_create_metatable(
                        metatable_bytes, sizeof metatable_bytes, &metatable) ||
                    fwrite(header_bytes, 1, sizeof header_bytes, db->fp) !=
                        sizeof header_bytes ||
                    fwrite(metatable_bytes, 1, sizeof metatable_bytes,
                           db->fp) != sizeof metatable_bytes ||
                    fflush(db->fp) != 0) {
                        database_close(db);
                        return false;
                }
                return true;
        }

        if (fseek(db->fp, 0, SEEK_END) != 0 ||
            (file_size = ftell(db->fp)) < 0 ||
            (uintmax_t)file_size > (uintmax_t)SIZE_MAX ||
            file_size < (long)(sizeof header_bytes + sizeof metatable_bytes) ||
            fseek(db->fp, 0, SEEK_SET) != 0 ||
            fread(header_bytes, 1, sizeof header_bytes, db->fp) !=
                sizeof header_bytes ||
            fread(metatable_bytes, 1, sizeof metatable_bytes, db->fp) !=
                sizeof metatable_bytes ||
            !executor_helper_read_header(header_bytes, sizeof header_bytes,
                                         &header) ||
            !executor_helper_read_metatable(
                metatable_bytes, sizeof metatable_bytes, &metatable) ||
            !executor_helper_validate_metadata_layout((size_t)file_size,
                                                      &header, &metatable)) {
                database_close(db);
                return false;
        }
        return true;
}

bool database_init_format(struct database* db, const char* path, bool created) {
        return database_init_format_mode(db, path, created, false);
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
        if (db->format_mode) {
                if (fflush(db->fp) != 0) {
                        return false;
                }
                db->modified = false;
                return true;
        }
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
