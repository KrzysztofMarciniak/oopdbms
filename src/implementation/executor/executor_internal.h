#ifndef IMPLEMENTATION_EXECUTOR_EXECUTOR_INTERNAL_H
#define IMPLEMENTATION_EXECUTOR_EXECUTOR_INTERNAL_H

#include <errno.h>
#include <float.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "implementation/executor/helper/binary_encoding.h"
#include "implementation/executor/helper/create_class_definition.h"
#include "implementation/executor/helper/create_field_descriptor.h"
#include "implementation/executor/helper/create_function_definition.h"
#include "implementation/executor/helper/create_header.h"
#include "implementation/executor/helper/create_interface_definition.h"
#include "implementation/executor/helper/create_object_record.h"
#include "implementation/executor/helper/create_table_entries.h"
#include "implementation/executor/helper/create_value_bytes.h"
#include "implementation/executor/helper/format_file.h"
#include "interface/executor/executor.h"

struct expression_text {
        char* bytes;
        size_t length;
        size_t capacity;
};

struct executor_class_view {
        uint8_t* bytes;
        const uint8_t* name;
        uint16_t name_length;
        uint16_t flags;
        uint32_t parent_class_id;
        uint32_t field_count;
        uint32_t interface_count;
        struct executor_format_field* fields;
        uint32_t* interface_ids;
};

struct executor_interface_view {
        uint8_t* bytes;
        uint16_t name_length;
        uint16_t field_count;
        struct executor_format_field* fields;
};

struct executor_value {
        uint8_t type_code;

        union {
                int64_t integer;
                double floating;
                bool boolean;

                struct {
                        uint32_t class_id;
                        uint64_t object_id;
                } pointer;

                struct {
                        const uint8_t* bytes;
                        size_t length;
                } text;
        } value;
};

struct executor_binding {
        const uint8_t* name;
        size_t name_length;
        struct executor_value value;
};

struct executor_eval_context {
        const struct executor_format_field* schema;
        size_t schema_count;
        const struct executor_format_object_field* fields;
        const struct executor_binding* bindings;
        size_t binding_count;
};

struct executor_text_parser {
        const uint8_t* bytes;
        size_t length;
        size_t position;
        struct executor_eval_context* context;
        char** allocations;
        size_t allocation_count;
};

struct executor_parameter_view {
        const uint8_t* name;
        uint16_t name_length;
        uint8_t type_code;
        uint32_t target_class_id;
};

struct executor_function_view {
        uint8_t* bytes;
        uint16_t name_length;
        uint16_t parameter_count;
        uint8_t return_type_code;
        uint32_t return_target_class_id;
        struct executor_parameter_view* parameters;
        const uint8_t* expression;
        uint32_t expression_length;
};

enum EXECUTOR_RESULT executor_schema_execute(
    struct database* db, const struct analyzer_statement* statement);
enum EXECUTOR_RESULT executor_functions_execute(
    struct database* db, const struct analyzer_statement* statement);
enum EXECUTOR_RESULT executor_objects_execute(
    struct database* db, const struct analyzer_statement* statement);
enum EXECUTOR_RESULT executor_metadata_execute(
    struct database* db, const struct analyzer_statement* statement);
bool _name_is(const uint8_t* definition, size_t length,
              size_t name_header_offset, size_t name_offset, const char* name);
void _class_view_free(struct executor_class_view* view);
void _interface_view_free(struct executor_interface_view* view);
bool _load_class_view(struct executor_format_file* file,
                      const struct executor_format_class_entry* entry,
                      struct executor_class_view* view);
bool _read_definition(const struct executor_format_file* file, uint64_t offset,
                      uint32_t length, uint8_t** definition);
bool _value_from_bytes(uint8_t type_code, const uint8_t* bytes, size_t length,
                       struct executor_value* value);
bool _find_class(struct executor_format_file* file, const char* name,
                 struct executor_format_class_entry* found);
bool _find_interface(struct executor_format_file* file, const char* name,
                     struct executor_format_interface_entry* found);
bool _encode_input_value(struct executor_format_file* file,
                         const struct executor_format_field* field,
                         const struct analyzer_value* input, uint8_t** output,
                         uint32_t* output_length);
bool _load_interface_view(struct executor_format_file* file,
                          const struct executor_format_interface_entry* entry,
                          struct executor_interface_view* view);
uint8_t _format_type(const struct analyzer_type* type);
bool _collect_fields(struct executor_format_file* file,
                     const struct analyzer_fields* input,
                     struct executor_format_field** fields,
                     uint32_t* max_field_id);
bool _class_name_by_id(struct executor_format_file* file, uint32_t class_id,
                       char** name);
bool _field_name_equal(const struct executor_format_field* field,
                       const char* name);
bool _print_value(struct executor_format_file* file,
                  const struct executor_value* value);
bool _field_types_equal(const struct executor_format_field* left,
                        const struct executor_format_field* right);
bool _find_function(struct executor_format_file* file, uint32_t owner_class_id,
                    const char* function_name, size_t* found_index,
                    struct executor_format_function_entry* found);
bool _class_field_exists(struct executor_format_file* file, uint32_t class_id,
                         const char* name, struct executor_format_field* found);
bool _evaluate_ast(const struct analyzer_expression* expression,
                   const struct executor_format_field* schema,
                   size_t schema_count,
                   const struct executor_format_object_field* fields,
                   struct executor_value* result);
bool _next_id(uint32_t max_id, uint32_t* next_id);
enum EXECUTOR_RESULT _call_function(struct database* db,
                                    const struct analyzer_statement* statement);
bool _class_field_by_bytes(struct executor_format_file* file, uint32_t class_id,
                           const uint8_t* name, uint16_t name_length,
                           struct executor_format_field* found);
bool _max_class_id(struct executor_format_file* file, uint32_t* max_id);
bool _schema_field_index(const struct executor_format_field* fields,
                         size_t count, const char* name, size_t* index);
bool _max_interface_id(struct executor_format_file* file, uint32_t* max_id);
enum EXECUTOR_RESULT executor_metadata_execute(
    struct database* db, const struct analyzer_statement* statement);
bool _max_field_id(struct executor_format_file* file, uint32_t* max_field_id);
bool _interface_fields_satisfied(
    struct executor_format_file* file,
    const struct executor_format_interface_entry* entry,
    const struct executor_format_field* fields, size_t field_count,
    uint32_t parent_class_id);
bool _class_or_descendant_has_objects(struct executor_format_file* file,
                                      uint32_t class_id);
bool _descendant_defines_field(struct executor_format_file* file,
                               uint32_t class_id, const char* name);
bool _append_new_table(struct executor_format_file* file,
                       const struct executor_format_section* old_table,
                       size_t old_count, size_t entry_size,
                       const uint8_t* new_entry, uint64_t* new_offset);
bool _append_table_bytes(struct executor_format_file* file,
                         const uint8_t* entries, size_t entry_size,
                         size_t count, uint64_t* new_offset);
bool _parse_i64(const char* text, int64_t* value);
bool _parse_u64(const char* text, uint64_t* value);
void _free_schema_fields(struct executor_format_field* fields, size_t count);
bool _parse_double(const char* text, double* value);
bool _collect_schema_fields(struct executor_format_file* file,
                            uint32_t class_id,
                            struct executor_format_field** output,
                            size_t* output_count);
void _free_object_fields(struct executor_format_object_field* fields,
                         size_t count);
bool _load_object_fields(struct executor_format_file* file,
                         const struct executor_format_class_entry* entry,
                         uint64_t object_id,
                         const struct executor_format_field* schema,
                         size_t schema_count, uint8_t** segment,
                         size_t* record_offset,
                         struct executor_format_object_header* header,
                         struct executor_format_object_field** fields);
bool _class_entry_by_id(struct executor_format_file* file, uint32_t class_id,
                        struct executor_format_class_entry* entry);
bool _interface_entry_by_id(struct executor_format_file* file,
                            uint32_t interface_id,
                            struct executor_format_interface_entry* entry);
bool _rewrite_class_entry(struct executor_format_file* file, size_t index,
                          const struct executor_format_class_entry* entry,
                          const uint8_t* definition, size_t definition_length);
bool _class_index_by_name(struct executor_format_file* file, const char* name,
                          size_t* index,
                          struct executor_format_class_entry* entry);
enum EXECUTOR_RESULT executor_functions_execute(
    struct database* db, const struct analyzer_statement* statement);
bool _interface_index_by_name(struct executor_format_file* file,
                              const char* name, size_t* index,
                              struct executor_format_interface_entry* entry);
bool _store_class_view(struct executor_format_file* file, size_t index,
                       const struct executor_format_class_entry* entry,
                       const struct executor_class_view* view);
bool _remove_class_table_entry(struct executor_format_file* file,
                               size_t remove_index);
bool _remove_interface_table_entry(struct executor_format_file* file,
                                   size_t remove_index);
enum EXECUTOR_RESULT executor_schema_execute(
    struct database* db, const struct analyzer_statement* statement);
bool _class_has_external_references(
    struct executor_format_file* file,
    const struct executor_format_class_entry* target);
enum EXECUTOR_RESULT executor_objects_execute(
    struct database* db, const struct analyzer_statement* statement);
bool _evaluate_text_expression(const uint8_t* expression,
                               size_t expression_length,
                               struct executor_eval_context* context,
                               struct executor_value* result);

#endif
