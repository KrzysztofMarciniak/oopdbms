/**
 * @file validate_metadata_layout.c
 * @brief Validate metadata table sizes, file ranges, and section overlap.
 */

#include "implementation/executor/helper/validate_metadata_layout.h"

#include "implementation/executor/helper/binary_encoding.h"
#include "implementation/executor/helper/create_header.h"
#include "implementation/executor/helper/create_metatable.h"
#include "implementation/executor/helper/create_table_entries.h"

static bool _section_valid(size_t file_size,
                           const struct executor_format_section* section,
                           uint64_t expected_length) {
        if (section->length != expected_length ||
            !executor_helper_range_valid(file_size, section->offset,
                                         section->length)) {
                return false;
        }
        return section->length == 0 ||
               section->offset >=
                   EXECUTOR_FORMAT_HEADER_SIZE + EXECUTOR_FORMAT_METATABLE_SIZE;
}

static bool _sections_overlap(const struct executor_format_section* left,
                              const struct executor_format_section* right) {
        if (left->length == 0 || right->length == 0) {
                return false;
        }
        return left->offset < right->offset + right->length &&
               right->offset < left->offset + left->length;
}

bool executor_helper_validate_metadata_layout(
    size_t file_size, const struct executor_format_header* header,
    const struct executor_format_metatable* metatable) {
        uint64_t interface_length;
        uint64_t class_length;
        uint64_t function_length;

        if (header == NULL || metatable == NULL ||
            file_size <
                EXECUTOR_FORMAT_HEADER_SIZE + EXECUTOR_FORMAT_METATABLE_SIZE ||
            !executor_helper_multiply_u64(header->interface_count,
                                          EXECUTOR_FORMAT_INTERFACE_ENTRY_SIZE,
                                          &interface_length) ||
            !executor_helper_multiply_u64(header->class_count,
                                          EXECUTOR_FORMAT_CLASS_ENTRY_SIZE,
                                          &class_length) ||
            !executor_helper_multiply_u64(header->function_count,
                                          EXECUTOR_FORMAT_FUNCTION_ENTRY_SIZE,
                                          &function_length)) {
                return false;
        }
        if (!_section_valid(file_size, &metatable->interfaces,
                            interface_length) ||
            !_section_valid(file_size, &metatable->classes, class_length) ||
            !_section_valid(file_size, &metatable->functions,
                            function_length)) {
                return false;
        }
        return !_sections_overlap(&metatable->interfaces,
                                  &metatable->classes) &&
               !_sections_overlap(&metatable->interfaces,
                                  &metatable->functions) &&
               !_sections_overlap(&metatable->classes, &metatable->functions);
}
