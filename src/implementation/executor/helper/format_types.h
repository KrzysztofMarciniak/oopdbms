/**
 * @file format_types.h
 * @brief Host-side views of the binary layouts specified in README.md.
 */

#ifndef IMPLEMENTATION_EXECUTOR_HELPER_FORMAT_TYPES_H
#define IMPLEMENTATION_EXECUTOR_HELPER_FORMAT_TYPES_H

#include <stddef.h>
#include <stdint.h>

enum EXECUTOR_FORMAT_TYPE {
        EXECUTOR_FORMAT_TYPE_INTEGER = 1,
        EXECUTOR_FORMAT_TYPE_FLOAT,
        EXECUTOR_FORMAT_TYPE_TEXT,
        EXECUTOR_FORMAT_TYPE_BOOL,
        EXECUTOR_FORMAT_TYPE_POINTER
};

struct executor_format_header {
        uint16_t interface_count;
        uint16_t class_count;
        uint16_t function_count;
};

struct executor_format_section {
        uint64_t offset;
        uint64_t length;
};

struct executor_format_metatable {
        struct executor_format_section interfaces;
        struct executor_format_section classes;
        struct executor_format_section functions;
};

struct executor_format_interface_entry {
        uint32_t interface_id;
        uint64_t definition_offset;
        uint32_t definition_length;
};

struct executor_format_class_entry {
        uint32_t class_id;
        uint64_t definition_offset;
        uint32_t definition_length;
        uint64_t data_offset;
        uint64_t data_length;
        uint64_t object_count;
        uint64_t next_object_id;
};

struct executor_format_function_entry {
        uint32_t function_id;
        uint32_t owner_class_id;
        uint64_t definition_offset;
        uint32_t definition_length;
};

struct executor_format_field {
        uint32_t field_id;
        uint8_t type_code;
        uint32_t target_class_id;
        const uint8_t* name;
        uint16_t name_length;
};

struct executor_format_interface_definition {
        const uint8_t* name;
        uint16_t name_length;
        const struct executor_format_field* fields;
        uint16_t field_count;
};

struct executor_format_class_definition {
        const uint8_t* name;
        uint16_t name_length;
        uint16_t flags;
        uint32_t parent_class_id;
        const struct executor_format_field* fields;
        uint32_t field_count;
        const uint32_t* interface_ids;
        uint32_t interface_count;
};

struct executor_format_parameter {
        const uint8_t* name;
        uint16_t name_length;
        uint8_t type_code;
        uint32_t target_class_id;
};

struct executor_format_function_definition {
        const uint8_t* name;
        uint16_t name_length;
        const struct executor_format_parameter* parameters;
        uint16_t parameter_count;
        uint8_t return_type_code;
        uint32_t return_target_class_id;
        const uint8_t* expression;
        uint32_t expression_length;
};

struct executor_format_object_field {
        uint32_t field_id;
        uint8_t type_code;
        const uint8_t* value;
        uint32_t value_length;
};

struct executor_format_object_record {
        uint64_t object_id;
        uint16_t flags;
        const struct executor_format_object_field* fields;
        uint16_t field_count;
};

struct executor_format_object_field_view {
        uint32_t field_id;
        uint8_t type_code;
        const uint8_t* value;
        uint32_t value_length;
};

#endif
