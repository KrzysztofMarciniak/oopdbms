/**
 * @file create_object_record.h
 * @brief Create and read object record headers and field records.
 */

#ifndef IMPLEMENTATION_EXECUTOR_HELPER_CREATE_OBJECT_RECORD_H
#define IMPLEMENTATION_EXECUTOR_HELPER_CREATE_OBJECT_RECORD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "implementation/executor/helper/format_types.h"

#define EXECUTOR_FORMAT_OBJECT_HEADER_SIZE 16u
#define EXECUTOR_FORMAT_OBJECT_LOG_NODE_HEADER_SIZE 12u
#define EXECUTOR_FORMAT_OBJECT_FLAG_DELETED 1u

struct executor_format_object_header {
        uint32_t record_length;
        uint64_t object_id;
        uint16_t flags;
        uint16_t field_count;
};

bool executor_helper_create_object_record(
    uint8_t* output, size_t capacity,
    const struct executor_format_object_record* record, size_t* written);
bool executor_helper_read_object_header(
    const uint8_t* bytes, size_t length,
    struct executor_format_object_header* header);
bool executor_helper_read_object_field(
    const uint8_t* bytes, size_t record_length, size_t* offset,
    struct executor_format_object_field_view* field);
bool executor_helper_compact_object_record_size(
    uint64_t object_id, uint16_t flags,
    const struct executor_format_object_field* fields, size_t field_count,
    size_t* size);
bool executor_helper_create_compact_object_record(
    uint8_t* output, size_t capacity, uint64_t object_id, uint16_t flags,
    const struct executor_format_object_field* fields, size_t field_count,
    size_t* written);
bool executor_helper_read_compact_object_header(
    const uint8_t* bytes, size_t record_length,
    struct executor_format_object_header* header, size_t* fields_offset);
bool executor_helper_read_compact_object_field(
    const uint8_t* bytes, size_t record_length, size_t* offset,
    const struct executor_format_field* schema_field,
    struct executor_format_object_field* field);

#endif
