/**
 * @file create_function_definition.c
 * @brief Create a function definition with its signature and source expression.
 */

#include "implementation/executor/helper/create_function_definition.h"

#include "implementation/executor/helper/binary_encoding.h"
#include "implementation/executor/helper/format_types.h"

static bool _valid_type(uint8_t type_code) {
        return type_code >= EXECUTOR_FORMAT_TYPE_INTEGER &&
               type_code <= EXECUTOR_FORMAT_TYPE_POINTER;
}

static size_t _type_descriptor_size(uint8_t type_code) {
        return type_code == EXECUTOR_FORMAT_TYPE_POINTER ? 5u : 1u;
}

static void _write_type_descriptor(uint8_t* output, uint8_t type_code,
                                   uint32_t target_class_id) {
        output[0] = type_code;
        if (type_code == EXECUTOR_FORMAT_TYPE_POINTER) {
                executor_helper_write_u32(output + 1, target_class_id);
        }
}

bool executor_helper_create_function_definition(
    uint8_t* output, size_t capacity,
    const struct executor_format_function_definition* definition,
    size_t* written) {
        uint64_t required;
        uint64_t next_size;
        size_t offset;
        size_t i;

        if (output == NULL || definition == NULL || written == NULL ||
            !_valid_type(definition->return_type_code) ||
            (definition->name_length != 0 && definition->name == NULL) ||
            (definition->parameter_count != 0 &&
             definition->parameters == NULL) ||
            (definition->expression_length != 0 &&
             definition->expression == NULL)) {
                return false;
        }
        required =
            2 + 2 + _type_descriptor_size(definition->return_type_code) + 4;
        if (!executor_helper_add_u64(required, definition->name_length,
                                     &next_size)) {
                return false;
        }
        required = next_size;
        for (i = 0; i < definition->parameter_count; i++) {
                const struct executor_format_parameter* parameter =
                    &definition->parameters[i];
                size_t parameter_size;

                if (!_valid_type(parameter->type_code) ||
                    (parameter->name_length != 0 && parameter->name == NULL)) {
                        return false;
                }
                parameter_size = 2 +
                                 _type_descriptor_size(parameter->type_code) +
                                 parameter->name_length;
                if (!executor_helper_add_u64(required, parameter_size,
                                             &next_size)) {
                        return false;
                }
                required = next_size;
        }
        if (!executor_helper_add_u64(required, definition->expression_length,
                                     &next_size)) {
                return false;
        }
        required = next_size;
        if (required > UINT32_MAX || capacity < required) {
                return false;
        }

        executor_helper_write_u16(output, definition->name_length);
        executor_helper_write_u16(output + 2, definition->parameter_count);
        _write_type_descriptor(output + 4, definition->return_type_code,
                               definition->return_target_class_id);
        offset = 4 + _type_descriptor_size(definition->return_type_code);
        executor_helper_write_u32(output + offset,
                                  definition->expression_length);
        offset += 4;
        for (i = 0; i < definition->name_length; i++) {
                output[offset + i] = definition->name[i];
        }
        offset += definition->name_length;
        for (i = 0; i < definition->parameter_count; i++) {
                const struct executor_format_parameter* parameter =
                    &definition->parameters[i];
                size_t parameter_type_size =
                    _type_descriptor_size(parameter->type_code);

                executor_helper_write_u16(output + offset,
                                          parameter->name_length);
                _write_type_descriptor(output + offset + 2,
                                       parameter->type_code,
                                       parameter->target_class_id);
                offset += 2 + parameter_type_size;
                for (size_t name_index = 0; name_index < parameter->name_length;
                     name_index++) {
                        output[offset + name_index] =
                            parameter->name[name_index];
                }
                offset += parameter->name_length;
        }
        for (i = 0; i < definition->expression_length; i++) {
                output[offset + i] = definition->expression[i];
        }
        offset += definition->expression_length;
        *written = offset;
        return true;
}
