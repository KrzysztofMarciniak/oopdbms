#ifndef INTERFACE_STORAGE_HEADER_H
#define INTERFACE_STORAGE_HEADER_H

/**
 * @file header.h
 * @brief Constants of the first bytes of an .oopdb file.
 *
 * On-disk layout (all integers little-endian):
 *  - bytes 0..7:   magic "OOPDB\0\0\0"
 *  - bytes 8..11:  version u32 (== 2)
 *  - bytes 12..15: page_size u32 (== 4096)
 *  - bytes 16..19: total_pages u32
 *  - bytes 20..23: free_list_head u32 (page index of first free page, 0 = none)
 *  - bytes 24..27: class_count u32
 *  - bytes 28..31: class_dir_page u32 (0 = none)
 *  - bytes 32..35: interface_count u32
 *  - bytes 36..39: interface_dir_page u32 (0 = none)
 *  - bytes 40..43: function_count u32
 *  - bytes 44..47: function_dir_page u32 (0 = none)
 *  - bytes 48..51: object_count u32 (global; may be unused)
 *  - bytes 52..55: object_dir_page u32 (reserved; 0)
 *  - bytes 56..59: reserved (0)
 */

#include <stdint.h>

/** File name used when the OOPDB environment variable is not set. */
#define STORAGE_DEFAULT_PATH "database.oopdb"

/** Magic bytes at the start of every database file: "OOPDB" and 3 zeros. */
#define STORAGE_MAGIC "OOPDB\0\0\0"

/** Length of STORAGE_MAGIC in bytes. */
#define STORAGE_MAGIC_SIZE 8U

/** Format version this build reads and writes. */
#define STORAGE_VERSION 2UL

/** Default page size in bytes. */
#define DB_PAGE_SIZE 4096U
#define STORAGE_PAGE_SIZE DB_PAGE_SIZE

/** Size of v2 header in bytes. */
#define STORAGE_HEADER_SIZE 60U

/** Invalid page number sentinel (no page). */
#define PAGE_INVALID 0xFFFFFFFFU

/* Header field byte offsets (v2) */
#define STORAGE_HDR_MAGIC_OFF 0U
#define STORAGE_HDR_VERSION_OFF 8U
#define STORAGE_HDR_PAGE_SIZE_OFF 12U
#define STORAGE_HDR_TOTAL_PAGES_OFF 16U
#define STORAGE_HDR_FREE_LIST_HEAD_OFF 20U
#define STORAGE_HDR_CLASS_COUNT_OFF 24U
#define STORAGE_HDR_CLASS_DIR_PAGE_OFF 28U
#define STORAGE_HDR_IFACE_COUNT_OFF 32U
#define STORAGE_HDR_IFACE_DIR_PAGE_OFF 36U
#define STORAGE_HDR_FUNC_COUNT_OFF 40U
#define STORAGE_HDR_FUNC_DIR_PAGE_OFF 44U
#define STORAGE_HDR_OBJ_COUNT_OFF 48U
#define STORAGE_HDR_OBJ_DIR_PAGE_OFF 52U
#define STORAGE_HDR_RESERVED_OFF 56U

#endif /* INTERFACE_STORAGE_HEADER_H */
