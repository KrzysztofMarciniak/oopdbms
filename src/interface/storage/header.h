#ifndef INTERFACE_STORAGE_HEADER_H
#define INTERFACE_STORAGE_HEADER_H

/**
 * @file header.h
 * @brief Constants of the first bytes of an .oopdb file.
 *
 * Layout, all integers little-endian:
 *  - bytes 0..7:  magic
 *  - bytes 8..11: format version (u32)
 */

/** File name used when the OOPDB environment variable is not set. */
#define STORAGE_DEFAULT_PATH "database.oopdb"

/** Magic bytes at the start of every database file: "OOPDB" and 3 zeros. */
#define STORAGE_MAGIC "OOPDB\0\0\0"

/** Length of STORAGE_MAGIC in bytes. */
#define STORAGE_MAGIC_SIZE 8

/** Format version this build reads and writes. */
#define STORAGE_VERSION 1ul

/** Bytes needed to check a file: magic and version. */
#define STORAGE_HEADER_SIZE 12

#endif// INTERFACE_STORAGE_HEADER_H
