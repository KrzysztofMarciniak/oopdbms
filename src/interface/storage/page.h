#ifndef INTERFACE_STORAGE_PAGE_H
#define INTERFACE_STORAGE_PAGE_H

/**
 * @file page.h
 * @brief Page constants and helpers for the page-based storage layer.
 */

#include <stdint.h>

#define DB_PAGE_SIZE 4096U
#define DB_HEADER_SIZE_V2 60U
#define PAGE_INVALID 0xFFFFFFFFU

/* Directory page header (on-disk, offsets within page) */
#define DIR_PAGE_COUNT_OFF 0U    /* u16 */
#define DIR_PAGE_FREE_OFF_OFF 2U /* u16 */
#define DIR_PAGE_NEXT_OFF 4U     /* u32 */
#define DIR_PAGE_HDR_SIZE 8U

/* Object page header (on-disk, offsets within page) */
#define OBJ_PAGE_NEXT_OFF 0U     /* u32 */
#define OBJ_PAGE_FREE_OFF_OFF 4U /* u32 */
#define OBJ_PAGE_HDR_SIZE 8U

/* Free page: first 4 bytes = next_free_page u32 (LE) */
#define FREE_PAGE_NEXT_OFF 0U /* u32 */

#endif /* INTERFACE_STORAGE_PAGE_H */
