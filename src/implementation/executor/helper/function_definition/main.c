
#include "implementation/executor/helper/function_definition/main.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "implementation/executor/helper/basic/main.h"
#include "implementation/executor/helper/binary_primitives/main.h"

static int helper_function_valid_type_code(uint8_t type_code) {
        return type_code >= HELPER_FUNCTION_TYPE_INTEGER &&
               type_code <= HELPER_FUNCTION_TYPE_POINTER;
}

static int helper_function_validate_type(
    const helper_function_type_descriptor* type) {
        if (type == NULL) {
                return -1;
        }

        if (!helper_function_valid_type_code(type->type_code)) {
                return -1;
        }

        return 0;
}

static int helper_function_write_type(
    FILE* file, const helper_function_type_descriptor* type) {
        if (file == NULL || helper_function_validate_type(type) != 0) {
                return -1;
        }

        if (helper_write_u8(file, type->type_code) != 0) {
                return -1;
        }

        if (type->type_code == HELPER_FUNCTION_TYPE_POINTER) {
                if (helper_write_u32(file, type->target_class_id) != 0) {
                        return -1;
                }
        }

        return 0;
}

static int helper_function_read_type(FILE* file,
                                     helper_function_type_descriptor* type) {
        helper_function_type_descriptor result = {0};

        if (file == NULL || type == NULL) {
                return -1;
        }

        if (helper_read_u8(file, &result.type_code) != 0) {
                return -1;
        }

        if (!helper_function_valid_type_code(result.type_code)) {
                return -1;
        }

        if (result.type_code == HELPER_FUNCTION_TYPE_POINTER) {
                if (helper_read_u32(file, &result.target_class_id) != 0) {
                        return -1;
                }
        }

        *type = result;
        return 0;
}

static int helper_function_read_name(FILE* file, uint16_t length, char** name) {
        char* result;

        if (file == NULL || name == NULL) {
                return -1;
        }

        result = malloc((size_t)length + 1u);
        if (result == NULL) {
                return -1;
        }

        if (helper_read_bytes(file, result, length) != 0) {
                free(result);
                return -1;
        }

        result[length] = '\0';
        *name          = result;

        return 0;
}

static int helper_function_validate_definition(
    const helper_function_definition* definition) {
        size_t i;

        if (definition == NULL || definition->name == NULL) {
                return -1;
        }

        if (definition->parameter_count > 0 && definition->parameters == NULL) {
                return -1;
        }

        if (definition->expression_length > 0 &&
            definition->expression == NULL) {
                return -1;
        }

        if (strlen(definition->name) > UINT16_MAX) {
                return -1;
        }

        if (helper_function_validate_type(&definition->return_type) != 0) {
                return -1;
        }

        for (i = 0; i < (size_t)definition->parameter_count; ++i) {
                const helper_function_parameter* parameter =
                    &definition->parameters[i];

                if (parameter->name == NULL) {
                        return -1;
                }

                if (strlen(parameter->name) > UINT16_MAX) {
                        return -1;
                }

                if (helper_function_validate_type(&parameter->type) != 0) {
                        return -1;
                }
        }

        return 0;
}

int helper_write_function_definition(
    FILE* file, const helper_function_definition* definition) {
        size_t name_length;
        size_t i;

        if (file == NULL ||
            helper_function_validate_definition(definition) != 0) {
                return -1;
        }

        name_length = strlen(definition->name);

        /* Fixed fields and return type descriptor. */
        if (helper_write_u16(file, (uint16_t)name_length) != 0) {
                return -1;
        }

        if (helper_write_u16(file, definition->parameter_count) != 0) {
                return -1;
        }

        if (helper_function_write_type(file, &definition->return_type) != 0) {
                return -1;
        }

        if (helper_write_u32(file, definition->expression_length) != 0) {
                return -1;
        }

        /* Function name bytes; no NUL terminator on disk. */
        if (helper_write_bytes(file, definition->name, name_length) != 0) {
                return -1;
        }

        /* Parameter descriptors. */
        for (i = 0; i < (size_t)definition->parameter_count; ++i) {
                const helper_function_parameter* parameter =
                    &definition->parameters[i];

                size_t parameter_name_length = strlen(parameter->name);

                if (helper_write_u16(file, (uint16_t)parameter_name_length) !=
                    0) {
                        return -1;
                }

                if (helper_function_write_type(file, &parameter->type) != 0) {
                        return -1;
                }

                if (helper_write_bytes(file, parameter->name,
                                       parameter_name_length) != 0) {
                        return -1;
                }
        }

        /* Expression is a length-delimited byte sequence. */
        if (definition->expression_length > 0) {
                if (helper_write_bytes(file, definition->expression,
                                       definition->expression_length) != 0) {
                        return -1;
                }
        }

        return 0;
}

void helper_free_function_definition(helper_function_definition* definition) {
        size_t i;

        if (definition == NULL) {
                return;
        }

        free(definition->name);
        definition->name = NULL;

        if (definition->parameters != NULL) {
                for (i = 0; i < (size_t)definition->parameter_count; ++i) {
                        free(definition->parameters[i].name);
                        definition->parameters[i].name = NULL;
                }

                free(definition->parameters);
                definition->parameters = NULL;
        }

        free(definition->expression);
        definition->expression = NULL;

        definition->parameter_count             = 0;
        definition->expression_length           = 0;
        definition->return_type.type_code       = 0;
        definition->return_type.target_class_id = 0;
}

int helper_read_function_definition(FILE* file,
                                    helper_function_definition* definition) {
        helper_function_definition result = {0};
        uint16_t name_length;
        size_t i;

        if (file == NULL || definition == NULL) {
                return -1;
        }

        if (helper_read_u16(file, &name_length) != 0) {
                goto failure;
        }

        if (helper_read_u16(file, &result.parameter_count) != 0) {
                goto failure;
        }

        if (helper_function_read_type(file, &result.return_type) != 0) {
                goto failure;
        }

        if (helper_read_u32(file, &result.expression_length) != 0) {
                goto failure;
        }

        if (helper_function_read_name(file, name_length, &result.name) != 0) {
                goto failure;
        }

        if (result.parameter_count > 0) {
                result.parameters = calloc((size_t)result.parameter_count,
                                           sizeof(*result.parameters));

                if (result.parameters == NULL) {
                        goto failure;
                }
        }

        /* Read parameter descriptors. */
        for (i = 0; i < (size_t)result.parameter_count; ++i) {
                helper_function_parameter* parameter = &result.parameters[i];
                uint16_t parameter_name_length;

                if (helper_read_u16(file, &parameter_name_length) != 0) {
                        goto failure;
                }

                if (helper_function_read_type(file, &parameter->type) != 0) {
                        goto failure;
                }

                if (helper_function_read_name(file, parameter_name_length,
                                              &parameter->name) != 0) {
                        goto failure;
                }
        }

        /* Read the expression bytes. They are not NUL-terminated. */
        if (result.expression_length > 0) {
                result.expression = malloc((size_t)result.expression_length);

                if (result.expression == NULL) {
                        goto failure;
                }

                if (helper_read_bytes(file, result.expression,
                                      result.expression_length) != 0) {
                        goto failure;
                }
        }

        *definition = result;
        return 0;

failure:
        helper_free_function_definition(&result);
        return -1;
}
