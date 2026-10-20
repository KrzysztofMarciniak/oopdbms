/**
 * @file executor_expression.c
 * @brief Executor expression implementation.
 */

#include "implementation/executor/executor_internal.h"

static bool _parse_text_expression(struct executor_text_parser* parser,
                                   struct executor_value* result);
static bool _evaluate_named_function(struct executor_text_parser* parser,
                                     const uint8_t* name, size_t name_length,
                                     const struct executor_value* arguments,
                                     size_t argument_count, bool evaluate,
                                     struct executor_value* result);
static bool _evaluate_script_body(const uint8_t* bytes, size_t length,
                                  struct executor_eval_context* context,
                                  struct executor_value* result);

static bool _read_double_le(const uint8_t* bytes, double* value) {
        static const uint8_t one_le[8] = {0, 0, 0, 0, 0, 0, 0xf0, 0x3f};
        static const uint8_t one_be[8] = {0x3f, 0xf0, 0, 0, 0, 0, 0, 0};
        uint8_t native[8];
        size_t i;

        if (sizeof(double) != 8 || DBL_MANT_DIG != 53 || DBL_MAX_EXP != 1024 ||
            DBL_MIN_EXP != -1021) {
                return false;
        }
        {
                double one = 1.0;
                memcpy(native, &one, sizeof native);
        }
        if (memcmp(native, one_le, 8) == 0) {
                memcpy(value, bytes, 8);
                return true;
        }
        if (memcmp(native, one_be, 8) == 0) {
                for (i = 0; i < 8; i++) {
                        native[i] = bytes[7 - i];
                }
                memcpy(value, native, 8);
                return true;
        }
        return false;
}

bool _value_from_bytes(uint8_t type_code, const uint8_t* bytes, size_t length,
                       struct executor_value* value) {
        if (value == NULL || (length != 0 && bytes == NULL)) {
                return false;
        }
        memset(value, 0, sizeof *value);
        value->type_code = type_code;
        switch (type_code) {
                case EXECUTOR_FORMAT_TYPE_INTEGER: {
                        uint64_t raw;
                        if (length != 8) {
                                return false;
                        }
                        raw = executor_helper_read_u64(bytes);
                        if (raw <= INT64_MAX) {
                                value->value.integer = (int64_t)raw;
                        } else {
                                value->value.integer =
                                    -1 - (int64_t)(UINT64_MAX - raw);
                        }
                        return true;
                }
                case EXECUTOR_FORMAT_TYPE_FLOAT:
                        return length == 8 &&
                               _read_double_le(bytes, &value->value.floating);
                case EXECUTOR_FORMAT_TYPE_TEXT:
                        value->value.text.bytes  = bytes;
                        value->value.text.length = length;
                        return true;
                case EXECUTOR_FORMAT_TYPE_BOOL:
                        if (length != 1 || bytes[0] > 1) {
                                return false;
                        }
                        value->value.boolean = bytes[0] != 0;
                        return true;
                case EXECUTOR_FORMAT_TYPE_POINTER:
                        if (length != 12) {
                                return false;
                        }
                        value->value.pointer.class_id =
                            executor_helper_read_u32(bytes);
                        value->value.pointer.object_id =
                            executor_helper_read_u64(bytes + 4);
                        return true;
                default:
                        return false;
        }
}

static bool _value_from_analyzer(const struct analyzer_value* input,
                                 struct executor_value* value) {
        int64_t integer;
        double floating;

        if (input == NULL || value == NULL) {
                return false;
        }
        memset(value, 0, sizeof *value);
        switch (input->kind) {
                case ANALYZER_VALUE_INTEGER:
                        if (!_parse_i64(input->text, &integer)) {
                                return false;
                        }
                        value->type_code     = EXECUTOR_FORMAT_TYPE_INTEGER;
                        value->value.integer = integer;
                        return true;
                case ANALYZER_VALUE_FLOAT:
                        if (!_parse_double(input->text, &floating)) {
                                return false;
                        }
                        value->type_code      = EXECUTOR_FORMAT_TYPE_FLOAT;
                        value->value.floating = floating;
                        return true;
                case ANALYZER_VALUE_TEXT:
                        value->type_code         = EXECUTOR_FORMAT_TYPE_TEXT;
                        value->value.text.bytes  = (const uint8_t*)input->text;
                        value->value.text.length = strlen(input->text);
                        return true;
                case ANALYZER_VALUE_BOOL:
                        if (input->text == NULL ||
                            (strcmp(input->text, "true") != 0 &&
                             strcmp(input->text, "false") != 0)) {
                                return false;
                        }
                        value->type_code     = EXECUTOR_FORMAT_TYPE_BOOL;
                        value->value.boolean = strcmp(input->text, "true") == 0;
                        return true;
                default:
                        return false;
        }
}

static bool _numeric_value(const struct executor_value* value, double* number,
                           bool* is_integer) {
        if (value->type_code == EXECUTOR_FORMAT_TYPE_INTEGER) {
                *number     = (double)value->value.integer;
                *is_integer = true;
                return true;
        }
        if (value->type_code == EXECUTOR_FORMAT_TYPE_FLOAT) {
                *number     = value->value.floating;
                *is_integer = false;
                return true;
        }
        return false;
}

static bool _multiply_i64(int64_t left, int64_t right, int64_t* output) {
        if (left > 0) {
                if ((right > 0 && left > INT64_MAX / right) ||
                    (right < 0 && right < INT64_MIN / left)) {
                        return false;
                }
        } else if (left < 0) {
                if ((right > 0 && left < INT64_MIN / right) ||
                    (right < 0 && right < INT64_MAX / left)) {
                        return false;
                }
        }
        *output = left * right;
        return true;
}

bool _evaluate_ast(const struct analyzer_expression* expression,
                   const struct executor_format_field* schema,
                   size_t schema_count,
                   const struct executor_format_object_field* fields,
                   struct executor_value* result) {
        struct executor_value left;
        struct executor_value right;
        size_t index;

        if (expression == NULL || result == NULL) {
                return false;
        }
        switch (expression->kind) {
                case ANALYZER_EXPRESSION_LITERAL:
                        return _value_from_analyzer(&expression->value, result);
                case ANALYZER_EXPRESSION_FIELD:
                        if (expression->field == NULL ||
                            !_schema_field_index(schema, schema_count,
                                                 expression->field, &index)) {
                                return false;
                        }
                        return _value_from_bytes(
                            fields[index].type_code, fields[index].value,
                            fields[index].value_length, result);
                case ANALYZER_EXPRESSION_NEGATE:
                        if (!_evaluate_ast(expression->left, schema,
                                           schema_count, fields, &left)) {
                                return false;
                        }
                        if (left.type_code == EXECUTOR_FORMAT_TYPE_INTEGER) {
                                if (left.value.integer == INT64_MIN) {
                                        return false;
                                }
                                result->type_code =
                                    EXECUTOR_FORMAT_TYPE_INTEGER;
                                result->value.integer = -left.value.integer;
                                return true;
                        }
                        if (left.type_code == EXECUTOR_FORMAT_TYPE_FLOAT) {
                                result->type_code = EXECUTOR_FORMAT_TYPE_FLOAT;
                                result->value.floating = -left.value.floating;
                                return true;
                        }
                        return false;
                case ANALYZER_EXPRESSION_BINARY:
                        if (!_evaluate_ast(expression->left, schema,
                                           schema_count, fields, &left) ||
                            !_evaluate_ast(expression->right, schema,
                                           schema_count, fields, &right)) {
                                return false;
                        }
                        break;
                default:
                        return false;
        }
        if (expression->op <= ANALYZER_OPERATOR_DIVIDE) {
                double left_number;
                double right_number;
                bool left_integer;
                bool right_integer;
                if (!_numeric_value(&left, &left_number, &left_integer) ||
                    !_numeric_value(&right, &right_number, &right_integer)) {
                        return false;
                }
                if (left_integer && right_integer &&
                    expression->op != ANALYZER_OPERATOR_DIVIDE) {
                        int64_t a = left.value.integer;
                        int64_t b = right.value.integer;
                        int64_t out;
                        if (expression->op == ANALYZER_OPERATOR_ADD) {
                                if ((b > 0 && a > INT64_MAX - b) ||
                                    (b < 0 && a < INT64_MIN - b)) {
                                        return false;
                                }
                                out = a + b;
                        } else if (expression->op ==
                                   ANALYZER_OPERATOR_SUBTRACT) {
                                if ((b < 0 && a > INT64_MAX + b) ||
                                    (b > 0 && a < INT64_MIN + b)) {
                                        return false;
                                }
                                out = a - b;
                        } else {
                                if (!_multiply_i64(a, b, &out)) {
                                        return false;
                                }
                        }
                        result->type_code     = EXECUTOR_FORMAT_TYPE_INTEGER;
                        result->value.integer = out;
                        return true;
                }
                if (expression->op == ANALYZER_OPERATOR_DIVIDE &&
                    right_number == 0.0) {
                        return false;
                }
                result->type_code = EXECUTOR_FORMAT_TYPE_FLOAT;
                switch (expression->op) {
                        case ANALYZER_OPERATOR_ADD:
                                result->value.floating =
                                    left_number + right_number;
                                break;
                        case ANALYZER_OPERATOR_SUBTRACT:
                                result->value.floating =
                                    left_number - right_number;
                                break;
                        case ANALYZER_OPERATOR_MULTIPLY:
                                result->value.floating =
                                    left_number * right_number;
                                break;
                        case ANALYZER_OPERATOR_DIVIDE:
                                result->value.floating =
                                    left_number / right_number;
                                break;
                        default:
                                return false;
                }
                return true;
        }
        {
                int comparison;
                double a;
                double b;
                bool ai;
                bool bi;
                if (left.type_code == EXECUTOR_FORMAT_TYPE_INTEGER &&
                    right.type_code == EXECUTOR_FORMAT_TYPE_INTEGER) {
                        comparison =
                            left.value.integer < right.value.integer   ? -1
                            : left.value.integer > right.value.integer ? 1
                                                                       : 0;
                } else if (_numeric_value(&left, &a, &ai) &&
                           _numeric_value(&right, &b, &bi)) {
                        comparison = a < b ? -1 : a > b ? 1 : 0;
                } else if (left.type_code != right.type_code) {
                        return false;
                } else if (left.type_code == EXECUTOR_FORMAT_TYPE_TEXT) {
                        size_t common =
                            left.value.text.length < right.value.text.length
                                ? left.value.text.length
                                : right.value.text.length;
                        comparison = memcmp(left.value.text.bytes,
                                            right.value.text.bytes, common);
                        if (comparison == 0) {
                                comparison = left.value.text.length <
                                                     right.value.text.length
                                                 ? -1
                                             : left.value.text.length >
                                                     right.value.text.length
                                                 ? 1
                                                 : 0;
                        }
                } else if (left.type_code == EXECUTOR_FORMAT_TYPE_BOOL) {
                        comparison =
                            (int)left.value.boolean - (int)right.value.boolean;
                } else if (left.type_code == EXECUTOR_FORMAT_TYPE_POINTER) {
                        if (left.value.pointer.class_id !=
                            right.value.pointer.class_id) {
                                comparison =
                                    left.value.pointer.class_id <
                                            right.value.pointer.class_id
                                        ? -1
                                        : 1;
                        } else {
                                comparison =
                                    left.value.pointer.object_id <
                                            right.value.pointer.object_id
                                        ? -1
                                    : left.value.pointer.object_id >
                                            right.value.pointer.object_id
                                        ? 1
                                        : 0;
                        }
                } else {
                        return false;
                }
                result->type_code = EXECUTOR_FORMAT_TYPE_BOOL;
                switch (expression->op) {
                        case ANALYZER_OPERATOR_EQUAL:
                                result->value.boolean = comparison == 0;
                                break;
                        case ANALYZER_OPERATOR_NOT_EQUAL:
                                result->value.boolean = comparison != 0;
                                break;
                        case ANALYZER_OPERATOR_LESS:
                                result->value.boolean = comparison < 0;
                                break;
                        case ANALYZER_OPERATOR_LESS_EQUAL:
                                result->value.boolean = comparison <= 0;
                                break;
                        case ANALYZER_OPERATOR_GREATER:
                                result->value.boolean = comparison > 0;
                                break;
                        case ANALYZER_OPERATOR_GREATER_EQUAL:
                                result->value.boolean = comparison >= 0;
                                break;
                        default:
                                return false;
                }
                return true;
        }
}

static void _text_parser_clear(struct executor_text_parser* parser) {
        size_t i;
        for (i = 0; i < parser->allocation_count; i++) {
                free(parser->allocations[i]);
        }
        free(parser->allocations);
}

static void _text_skip_space(struct executor_text_parser* parser) {
        while (parser->position < parser->length &&
               (parser->bytes[parser->position] == ' ' ||
                parser->bytes[parser->position] == '\t' ||
                parser->bytes[parser->position] == '\n' ||
                parser->bytes[parser->position] == '\r')) {
                parser->position++;
        }
}

static bool _resolve_identifier(struct executor_eval_context* context,
                                const uint8_t* name, size_t name_length,
                                struct executor_value* value) {
        size_t i;

        for (i = context->local_count; i > 0; i--) {
                if (context->locals[i - 1].name_length == name_length &&
                    memcmp(context->locals[i - 1].name, name, name_length) ==
                        0) {
                        *value = context->locals[i - 1].value;
                        return true;
                }
        }
        for (i = 0; i < context->binding_count; i++) {
                if (context->bindings[i].name_length == name_length &&
                    memcmp(context->bindings[i].name, name, name_length) == 0) {
                        *value = context->bindings[i].value;
                        return true;
                }
        }
        for (i = 0; i < context->schema_count; i++) {
                if (context->schema[i].name_length == name_length &&
                    memcmp(context->schema[i].name, name, name_length) == 0) {
                        return _value_from_bytes(
                            context->fields[i].type_code,
                            context->fields[i].value,
                            context->fields[i].value_length, value);
                }
        }
        return false;
}

static bool _parser_track_text(struct executor_text_parser* parser,
                               char* text) {
        char** grown =
            realloc(parser->allocations, (parser->allocation_count + 1) *
                                             sizeof *parser->allocations);
        if (grown == NULL) {
                free(text);
                return false;
        }
        parser->allocations                             = grown;
        parser->allocations[parser->allocation_count++] = text;
        return true;
}

static bool _parse_text_primary(struct executor_text_parser* parser,
                                struct executor_value* result) {
        size_t start;
        uint8_t ch;

        _text_skip_space(parser);
        if (parser->position >= parser->length) {
                return false;
        }
        ch = parser->bytes[parser->position];
        if (ch == '(') {
                parser->position++;
                if (!_parse_text_expression(parser, result)) {
                        return false;
                }
                _text_skip_space(parser);
                if (parser->position >= parser->length ||
                    parser->bytes[parser->position++] != ')') {
                        return false;
                }
                return true;
        }
        if (ch == '"' || ch == '\'') {
                uint8_t quote = ch;
                char* text;
                size_t text_length = 0;
                size_t capacity    = parser->length - parser->position;

                parser->position++;
                text = malloc(capacity + 1);
                if (text == NULL) {
                        return false;
                }
                while (parser->position < parser->length &&
                       parser->bytes[parser->position] != quote) {
                        ch = parser->bytes[parser->position++];
                        if (ch == '\\') {
                                if (parser->position >= parser->length) {
                                        free(text);
                                        return false;
                                }
                                ch = parser->bytes[parser->position++];
                                if (ch != '\\' && ch != '"' && ch != '\'') {
                                        free(text);
                                        return false;
                                }
                        }
                        text[text_length++] = (char)ch;
                }
                if (parser->position >= parser->length) {
                        free(text);
                        return false;
                }
                parser->position++;
                text[text_length] = '\0';
                if (!_parser_track_text(parser, text)) {
                        return false;
                }
                result->type_code         = EXECUTOR_FORMAT_TYPE_TEXT;
                result->value.text.bytes  = (const uint8_t*)text;
                result->value.text.length = text_length;
                return true;
        }
        if ((ch >= '0' && ch <= '9')) {
                bool is_float = false;
                char* number;
                double floating;
                int64_t integer;

                start = parser->position;
                while (parser->position < parser->length) {
                        ch = parser->bytes[parser->position];
                        if (ch >= '0' && ch <= '9') {
                                parser->position++;
                        } else if (ch == '.' && !is_float) {
                                is_float = true;
                                parser->position++;
                        } else {
                                break;
                        }
                }
                number = malloc(parser->position - start + 1);
                if (number == NULL) {
                        return false;
                }
                memcpy(number, parser->bytes + start, parser->position - start);
                number[parser->position - start] = '\0';
                if (is_float) {
                        if (!_parse_double(number, &floating)) {
                                free(number);
                                return false;
                        }
                        result->type_code      = EXECUTOR_FORMAT_TYPE_FLOAT;
                        result->value.floating = floating;
                } else {
                        if (!_parse_i64(number, &integer)) {
                                free(number);
                                return false;
                        }
                        result->type_code     = EXECUTOR_FORMAT_TYPE_INTEGER;
                        result->value.integer = integer;
                }
                free(number);
                return true;
        }
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_') {
                size_t name_length;
                start = parser->position++;
                while (parser->position < parser->length) {
                        ch = parser->bytes[parser->position];
                        if ((ch >= 'A' && ch <= 'Z') ||
                            (ch >= 'a' && ch <= 'z') ||
                            (ch >= '0' && ch <= '9') || ch == '_') {
                                parser->position++;
                        } else {
                                break;
                        }
                }
                if (parser->position - start == 4 &&
                    memcmp(parser->bytes + start, "true", 4) == 0) {
                        result->type_code     = EXECUTOR_FORMAT_TYPE_BOOL;
                        result->value.boolean = true;
                        return true;
                }
                if (parser->position - start == 5 &&
                    memcmp(parser->bytes + start, "false", 5) == 0) {
                        result->type_code     = EXECUTOR_FORMAT_TYPE_BOOL;
                        result->value.boolean = false;
                        return true;
                }
                name_length = parser->position - start;
                _text_skip_space(parser);
                if (parser->position < parser->length &&
                    parser->bytes[parser->position] == '(') {
                        struct executor_value* arguments = NULL;
                        size_t argument_count            = 0;
                        size_t argument_capacity         = 0;
                        bool ok                          = true;

                        parser->position++;
                        _text_skip_space(parser);
                        while (parser->position < parser->length &&
                               parser->bytes[parser->position] != ')') {
                                struct executor_value* grown;
                                if (argument_count == argument_capacity) {
                                        size_t capacity =
                                            argument_capacity == 0
                                                ? 4
                                                : argument_capacity * 2;
                                        if (capacity >
                                            SIZE_MAX / sizeof *arguments) {
                                                free(arguments);
                                                return false;
                                        }
                                        grown = realloc(
                                            arguments,
                                            capacity * sizeof *arguments);
                                        if (grown == NULL) {
                                                free(arguments);
                                                return false;
                                        }
                                        arguments         = grown;
                                        argument_capacity = capacity;
                                }
                                if (!_parse_text_expression(
                                        parser, &arguments[argument_count++])) {
                                        free(arguments);
                                        return false;
                                }
                                _text_skip_space(parser);
                                if (parser->position < parser->length &&
                                    parser->bytes[parser->position] == ',') {
                                        parser->position++;
                                        continue;
                                }
                                break;
                        }
                        _text_skip_space(parser);
                        if (parser->position >= parser->length ||
                            parser->bytes[parser->position++] != ')') {
                                free(arguments);
                                return false;
                        }
                        ok = _evaluate_named_function(
                            parser, parser->bytes + start, name_length,
                            arguments, argument_count, parser->evaluate,
                            result);
                        free(arguments);
                        return ok;
                }
                return _resolve_identifier(parser->context,
                                           parser->bytes + start, name_length,
                                           result);
        }
        return false;
}

static bool _apply_text_operator(enum ANALYZER_OPERATOR op,
                                 const struct executor_value* left,
                                 const struct executor_value* right,
                                 struct executor_value* result) {
        if (op <= ANALYZER_OPERATOR_DIVIDE) {
                double a;
                double b;
                bool ai;
                bool bi;
                if (!_numeric_value(left, &a, &ai) ||
                    !_numeric_value(right, &b, &bi)) {
                        return false;
                }
                if (ai && bi && op != ANALYZER_OPERATOR_DIVIDE) {
                        int64_t x = left->value.integer;
                        int64_t y = right->value.integer;
                        int64_t value;
                        if (op == ANALYZER_OPERATOR_ADD) {
                                if ((y > 0 && x > INT64_MAX - y) ||
                                    (y < 0 && x < INT64_MIN - y)) {
                                        return false;
                                }
                                value = x + y;
                        } else if (op == ANALYZER_OPERATOR_SUBTRACT) {
                                if ((y < 0 && x > INT64_MAX + y) ||
                                    (y > 0 && x < INT64_MIN + y)) {
                                        return false;
                                }
                                value = x - y;
                        } else {
                                if (!_multiply_i64(x, y, &value)) {
                                        return false;
                                }
                        }
                        result->type_code     = EXECUTOR_FORMAT_TYPE_INTEGER;
                        result->value.integer = value;
                        return true;
                }
                if (op == ANALYZER_OPERATOR_DIVIDE && b == 0) {
                        return false;
                }
                result->type_code = EXECUTOR_FORMAT_TYPE_FLOAT;
                switch (op) {
                        case ANALYZER_OPERATOR_ADD:
                                result->value.floating = a + b;
                                break;
                        case ANALYZER_OPERATOR_SUBTRACT:
                                result->value.floating = a - b;
                                break;
                        case ANALYZER_OPERATOR_MULTIPLY:
                                result->value.floating = a * b;
                                break;
                        case ANALYZER_OPERATOR_DIVIDE:
                                result->value.floating = a / b;
                                break;
                        default:
                                return false;
                }
                return true;
        }
        {
                int comparison;
                double a;
                double b;
                bool ai;
                bool bi;
                if (left->type_code == EXECUTOR_FORMAT_TYPE_INTEGER &&
                    right->type_code == EXECUTOR_FORMAT_TYPE_INTEGER) {
                        comparison =
                            left->value.integer < right->value.integer   ? -1
                            : left->value.integer > right->value.integer ? 1
                                                                         : 0;
                } else if (_numeric_value(left, &a, &ai) &&
                           _numeric_value(right, &b, &bi)) {
                        comparison = a < b ? -1 : a > b ? 1 : 0;
                } else if (left->type_code != right->type_code) {
                        return false;
                } else if (left->type_code == EXECUTOR_FORMAT_TYPE_TEXT) {
                        size_t common =
                            left->value.text.length < right->value.text.length
                                ? left->value.text.length
                                : right->value.text.length;
                        comparison = memcmp(left->value.text.bytes,
                                            right->value.text.bytes, common);
                        if (comparison == 0) {
                                comparison = left->value.text.length <
                                                     right->value.text.length
                                                 ? -1
                                             : left->value.text.length >
                                                     right->value.text.length
                                                 ? 1
                                                 : 0;
                        }
                } else if (left->type_code == EXECUTOR_FORMAT_TYPE_BOOL) {
                        comparison = (int)left->value.boolean -
                                     (int)right->value.boolean;
                } else if (left->type_code == EXECUTOR_FORMAT_TYPE_POINTER) {
                        comparison = left->value.pointer.class_id <
                                             right->value.pointer.class_id
                                         ? -1
                                     : left->value.pointer.class_id >
                                             right->value.pointer.class_id
                                         ? 1
                                     : left->value.pointer.object_id <
                                             right->value.pointer.object_id
                                         ? -1
                                     : left->value.pointer.object_id >
                                             right->value.pointer.object_id
                                         ? 1
                                         : 0;
                } else {
                        return false;
                }
                result->type_code = EXECUTOR_FORMAT_TYPE_BOOL;
                switch (op) {
                        case ANALYZER_OPERATOR_EQUAL:
                                result->value.boolean = comparison == 0;
                                break;
                        case ANALYZER_OPERATOR_NOT_EQUAL:
                                result->value.boolean = comparison != 0;
                                break;
                        case ANALYZER_OPERATOR_LESS:
                                result->value.boolean = comparison < 0;
                                break;
                        case ANALYZER_OPERATOR_LESS_EQUAL:
                                result->value.boolean = comparison <= 0;
                                break;
                        case ANALYZER_OPERATOR_GREATER:
                                result->value.boolean = comparison > 0;
                                break;
                        case ANALYZER_OPERATOR_GREATER_EQUAL:
                                result->value.boolean = comparison >= 0;
                                break;
                        default:
                                return false;
                }
                return true;
        }
}

static bool _infer_text_operator(enum ANALYZER_OPERATOR op,
                                 const struct executor_value* left,
                                 const struct executor_value* right,
                                 struct executor_value* result) {
        double number;
        bool integer;

        memset(result, 0, sizeof *result);
        if (op <= ANALYZER_OPERATOR_DIVIDE) {
                if (!_numeric_value(left, &number, &integer) ||
                    !_numeric_value(right, &number, &integer)) {
                        return false;
                }
                result->type_code =
                    left->type_code == EXECUTOR_FORMAT_TYPE_INTEGER &&
                            right->type_code == EXECUTOR_FORMAT_TYPE_INTEGER &&
                            op != ANALYZER_OPERATOR_DIVIDE
                        ? EXECUTOR_FORMAT_TYPE_INTEGER
                        : EXECUTOR_FORMAT_TYPE_FLOAT;
                return true;
        }
        if (left->type_code != right->type_code &&
            !(_numeric_value(left, &number, &integer) &&
              _numeric_value(right, &number, &integer))) {
                return false;
        }
        if (left->type_code == EXECUTOR_FORMAT_TYPE_POINTER &&
            left->value.pointer.class_id != right->value.pointer.class_id) {
                return false;
        }
        result->type_code = EXECUTOR_FORMAT_TYPE_BOOL;
        return true;
}

static bool _parse_text_unary(struct executor_text_parser* parser,
                              struct executor_value* result) {
        _text_skip_space(parser);
        if (parser->position < parser->length &&
            parser->bytes[parser->position] == '!') {
                struct executor_value operand;
                bool ok;
                if (parser->depth >= EXECUTOR_FUNCTION_EXPRESSION_DEPTH_LIMIT) {
                        return false;
                }
                parser->position++;
                parser->depth++;
                ok = _parse_text_unary(parser, &operand);
                parser->depth--;
                if (!ok || operand.type_code != EXECUTOR_FORMAT_TYPE_BOOL) {
                        return false;
                }
                result->type_code = EXECUTOR_FORMAT_TYPE_BOOL;
                result->value.boolean =
                    parser->evaluate ? !operand.value.boolean : false;
                return true;
        }
        if (parser->position < parser->length &&
            parser->bytes[parser->position] == '-') {
                struct executor_value operand;
                bool ok;
                if (parser->depth >= EXECUTOR_FUNCTION_EXPRESSION_DEPTH_LIMIT) {
                        return false;
                }
                parser->position++;
                parser->depth++;
                ok = _parse_text_unary(parser, &operand);
                parser->depth--;
                if (!ok) {
                        return false;
                }
                if (!parser->evaluate) {
                        if (operand.type_code != EXECUTOR_FORMAT_TYPE_INTEGER &&
                            operand.type_code != EXECUTOR_FORMAT_TYPE_FLOAT) {
                                return false;
                        }
                        *result = operand;
                        return true;
                }
                if (operand.type_code == EXECUTOR_FORMAT_TYPE_INTEGER) {
                        if (operand.value.integer == INT64_MIN) {
                                return false;
                        }
                        result->type_code     = EXECUTOR_FORMAT_TYPE_INTEGER;
                        result->value.integer = -operand.value.integer;
                        return true;
                }
                if (operand.type_code == EXECUTOR_FORMAT_TYPE_FLOAT) {
                        result->type_code      = EXECUTOR_FORMAT_TYPE_FLOAT;
                        result->value.floating = -operand.value.floating;
                        return true;
                }
                return false;
        }
        return _parse_text_primary(parser, result);
}

static bool _parse_text_term(struct executor_text_parser* parser,
                             struct executor_value* result) {
        struct executor_value left;

        if (!_parse_text_unary(parser, &left)) {
                return false;
        }
        for (;;) {
                struct executor_value right;
                struct executor_value combined;
                enum ANALYZER_OPERATOR op;
                uint8_t ch;

                _text_skip_space(parser);
                if (parser->position >= parser->length) {
                        break;
                }
                ch = parser->bytes[parser->position];
                if (ch != '*' && ch != '/') {
                        break;
                }
                parser->position++;
                op = ch == '*' ? ANALYZER_OPERATOR_MULTIPLY
                               : ANALYZER_OPERATOR_DIVIDE;
                if (!_parse_text_unary(parser, &right)) {
                        return false;
                }
                if (parser->evaluate) {
                        if (!_apply_text_operator(op, &left, &right,
                                                  &combined)) {
                                return false;
                        }
                        left = combined;
                } else {
                        if (!_infer_text_operator(op, &left, &right,
                                                  &combined)) {
                                return false;
                        }
                        left = combined;
                }
        }
        *result = left;
        return true;
}

static bool _parse_text_additive(struct executor_text_parser* parser,
                                 struct executor_value* result) {
        struct executor_value left;

        if (!_parse_text_term(parser, &left)) {
                return false;
        }
        for (;;) {
                struct executor_value right;
                struct executor_value combined;
                enum ANALYZER_OPERATOR op;
                uint8_t ch;

                _text_skip_space(parser);
                if (parser->position >= parser->length) {
                        break;
                }
                ch = parser->bytes[parser->position];
                if (ch != '+' && ch != '-') {
                        break;
                }
                parser->position++;
                op = ch == '+' ? ANALYZER_OPERATOR_ADD
                               : ANALYZER_OPERATOR_SUBTRACT;
                if (!_parse_text_term(parser, &right)) {
                        return false;
                }
                if (parser->evaluate) {
                        if (!_apply_text_operator(op, &left, &right,
                                                  &combined)) {
                                return false;
                        }
                        left = combined;
                } else {
                        if (!_infer_text_operator(op, &left, &right,
                                                  &combined)) {
                                return false;
                        }
                        left = combined;
                }
        }
        *result = left;
        return true;
}

static bool _parse_text_comparison(struct executor_text_parser* parser,
                                   struct executor_value* result) {
        struct executor_value left;
        struct executor_value right;
        struct executor_value combined;
        enum ANALYZER_OPERATOR op;
        uint8_t ch;

        if (!_parse_text_additive(parser, &left)) {
                return false;
        }
        _text_skip_space(parser);
        if (parser->position >= parser->length) {
                *result = left;
                return true;
        }
        ch = parser->bytes[parser->position];
        if (ch == '=') {
                parser->position++;
                if (parser->position < parser->length &&
                    parser->bytes[parser->position] == '=') {
                        return false;
                }
                op = ANALYZER_OPERATOR_EQUAL;
        } else if (ch == '!') {
                if (parser->position + 1 >= parser->length ||
                    parser->bytes[parser->position + 1] != '=') {
                        return false;
                }
                parser->position += 2;
                op = ANALYZER_OPERATOR_NOT_EQUAL;
        } else if (ch == '<' || ch == '>') {
                parser->position++;
                if (parser->position < parser->length &&
                    parser->bytes[parser->position] == '=') {
                        parser->position++;
                        op = ch == '<' ? ANALYZER_OPERATOR_LESS_EQUAL
                                       : ANALYZER_OPERATOR_GREATER_EQUAL;
                } else {
                        op = ch == '<' ? ANALYZER_OPERATOR_LESS
                                       : ANALYZER_OPERATOR_GREATER;
                }
        } else {
                *result = left;
                return true;
        }
        if (!_parse_text_additive(parser, &right)) {
                return false;
        }
        if (!parser->evaluate) {
                return _infer_text_operator(op, &left, &right, result);
        }
        if (!_apply_text_operator(op, &left, &right, &combined)) {
                return false;
        }
        *result = combined;
        return true;
}

static bool _parse_text_and(struct executor_text_parser* parser,
                            struct executor_value* result) {
        struct executor_value left;

        if (!_parse_text_comparison(parser, &left)) {
                return false;
        }
        for (;;) {
                struct executor_value right;
                bool evaluate_right;

                _text_skip_space(parser);
                if (parser->position + 1 >= parser->length ||
                    parser->bytes[parser->position] != '&' ||
                    parser->bytes[parser->position + 1] != '&') {
                        break;
                }
                if (left.type_code != EXECUTOR_FORMAT_TYPE_BOOL) {
                        return false;
                }
                parser->position += 2;
                evaluate_right = parser->evaluate && left.value.boolean;
                {
                        bool was_evaluating = parser->evaluate;
                        parser->evaluate    = evaluate_right;
                        if (!_parse_text_comparison(parser, &right)) {
                                parser->evaluate = was_evaluating;
                                return false;
                        }
                        parser->evaluate = was_evaluating;
                }
                if (right.type_code != EXECUTOR_FORMAT_TYPE_BOOL) {
                        return false;
                }
                left.type_code     = EXECUTOR_FORMAT_TYPE_BOOL;
                left.value.boolean = parser->evaluate && left.value.boolean &&
                                     evaluate_right && right.value.boolean;
        }
        *result = left;
        return true;
}

static bool _parse_text_expression_inner(struct executor_text_parser* parser,
                                         struct executor_value* result) {
        struct executor_value left;

        if (!_parse_text_and(parser, &left)) {
                return false;
        }
        for (;;) {
                struct executor_value right;
                bool evaluate_right;

                _text_skip_space(parser);
                if (parser->position + 1 >= parser->length ||
                    parser->bytes[parser->position] != '|' ||
                    parser->bytes[parser->position + 1] != '|') {
                        break;
                }
                if (left.type_code != EXECUTOR_FORMAT_TYPE_BOOL) {
                        return false;
                }
                parser->position += 2;
                evaluate_right = parser->evaluate && !left.value.boolean;
                {
                        bool was_evaluating = parser->evaluate;
                        parser->evaluate    = evaluate_right;
                        if (!_parse_text_and(parser, &right)) {
                                parser->evaluate = was_evaluating;
                                return false;
                        }
                        parser->evaluate = was_evaluating;
                }
                if (right.type_code != EXECUTOR_FORMAT_TYPE_BOOL) {
                        return false;
                }
                left.type_code     = EXECUTOR_FORMAT_TYPE_BOOL;
                left.value.boolean = parser->evaluate &&
                                     (left.value.boolean ||
                                      (evaluate_right && right.value.boolean));
        }
        *result = left;
        return true;
}

static bool _parse_text_expression(struct executor_text_parser* parser,
                                   struct executor_value* result) {
        bool ok;

        if (parser->depth >= EXECUTOR_FUNCTION_EXPRESSION_DEPTH_LIMIT) {
                return false;
        }
        parser->depth++;
        ok = _parse_text_expression_inner(parser, result);
        parser->depth--;
        return ok;
}

static bool _context_copy_text(struct executor_eval_context* context,
                               struct executor_value* value) {
        struct executor_text_pool* pool = context->text_pool;
        uint8_t* copy;

        if (value->type_code != EXECUTOR_FORMAT_TYPE_TEXT || pool == NULL) {
                return true;
        }
        copy = malloc(value->value.text.length == 0 ? 1
                                                    : value->value.text.length);
        if (copy == NULL) {
                return false;
        }
        if (value->value.text.length != 0) {
                memcpy(copy, value->value.text.bytes, value->value.text.length);
        }
        if (pool->count == pool->capacity) {
                size_t capacity = pool->capacity == 0 ? 8 : pool->capacity * 2;
                uint8_t** grown;

                if (capacity > SIZE_MAX / sizeof *pool->items) {
                        free(copy);
                        return false;
                }
                grown = realloc(pool->items, capacity * sizeof *pool->items);
                if (grown == NULL) {
                        free(copy);
                        return false;
                }
                pool->items    = grown;
                pool->capacity = capacity;
        }
        pool->items[pool->count++] = copy;
        value->value.text.bytes    = copy;
        return true;
}

static bool _evaluate_expression_span(const uint8_t* bytes, size_t start,
                                      size_t end,
                                      struct executor_eval_context* context,
                                      bool evaluate,
                                      struct executor_value* result) {
        struct executor_text_parser parser;
        bool ok;

        memset(&parser, 0, sizeof parser);
        parser.bytes    = bytes;
        parser.length   = end;
        parser.position = start;
        parser.context  = context;
        parser.evaluate = evaluate;
        ok              = _parse_text_expression(&parser, result);
        if (ok) {
                _text_skip_space(&parser);
                ok = parser.position == end;
        }
        if (ok && evaluate && !_context_copy_text(context, result)) {
                ok = false;
        }
        _text_parser_clear(&parser);
        return ok;
}

bool _evaluate_text_expression(const uint8_t* expression,
                               size_t expression_length,
                               struct executor_eval_context* context,
                               struct executor_value* result) {
        size_t start = 0;

        while (start < expression_length &&
               (expression[start] == ' ' || expression[start] == '\t' ||
                expression[start] == '\n' || expression[start] == '\r')) {
                start++;
        }
        if (start < expression_length && expression[start] == '{') {
                return _evaluate_script_body(expression, expression_length,
                                             context, result);
        }
        return _evaluate_expression_span(expression, start, expression_length,
                                         context, true, result);
}

struct executor_script_parser {
        const uint8_t* bytes;
        size_t length;
        size_t position;
        struct executor_eval_context* context;
        bool execute;
        bool* returned;
        bool* definitely_returns;
        struct executor_value* result;
};

static void _script_skip_space(struct executor_script_parser* parser) {
        while (parser->position < parser->length) {
                uint8_t ch = parser->bytes[parser->position];
                if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') {
                        break;
                }
                parser->position++;
        }
}

static bool _script_word_char(uint8_t ch) {
        return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
               (ch >= '0' && ch <= '9') || ch == '_';
}

static bool _script_take_word(struct executor_script_parser* parser,
                              size_t* start, size_t* length) {
        _script_skip_space(parser);
        *start = parser->position;
        if (parser->position >= parser->length ||
            !((parser->bytes[parser->position] >= 'A' &&
               parser->bytes[parser->position] <= 'Z') ||
              (parser->bytes[parser->position] >= 'a' &&
               parser->bytes[parser->position] <= 'z') ||
              parser->bytes[parser->position] == '_')) {
                return false;
        }
        parser->position++;
        while (parser->position < parser->length &&
               _script_word_char(parser->bytes[parser->position])) {
                parser->position++;
        }
        *length = parser->position - *start;
        return true;
}

static bool _script_word_is(const uint8_t* bytes, size_t start, size_t length,
                            const char* word) {
        size_t word_length = strlen(word);
        return length == word_length &&
               memcmp(bytes + start, word, word_length) == 0;
}

static bool _script_take_delimiter(struct executor_script_parser* parser,
                                   uint8_t delimiter) {
        _script_skip_space(parser);
        if (parser->position >= parser->length ||
            parser->bytes[parser->position] != delimiter) {
                return false;
        }
        parser->position++;
        return true;
}

static bool _script_read_group(struct executor_script_parser* parser,
                               uint8_t open, uint8_t close, size_t* inner_start,
                               size_t* inner_end) {
        size_t depth  = 1;
        uint8_t quote = 0;
        bool escaped  = false;

        _script_skip_space(parser);
        if (parser->position >= parser->length ||
            parser->bytes[parser->position++] != open) {
                return false;
        }
        *inner_start = parser->position;
        while (parser->position < parser->length) {
                uint8_t ch = parser->bytes[parser->position++];

                if (quote != 0) {
                        if (escaped) {
                                escaped = false;
                        } else if (ch == '\\') {
                                escaped = true;
                        } else if (ch == quote) {
                                quote = 0;
                        }
                        continue;
                }
                if (ch == '"' || ch == '\'') {
                        quote = ch;
                } else if (ch == open) {
                        depth++;
                } else if (ch == close && --depth == 0) {
                        *inner_end = parser->position - 1;
                        return true;
                }
        }
        return false;
}

static bool _script_read_expression(struct executor_script_parser* parser,
                                    uint8_t delimiter, size_t* start,
                                    size_t* end) {
        size_t parens = 0;
        uint8_t quote = 0;
        bool escaped  = false;

        _script_skip_space(parser);
        *start = parser->position;
        while (parser->position < parser->length) {
                uint8_t ch = parser->bytes[parser->position];

                if (quote != 0) {
                        parser->position++;
                        if (escaped) {
                                escaped = false;
                        } else if (ch == '\\') {
                                escaped = true;
                        } else if (ch == quote) {
                                quote = 0;
                        }
                        continue;
                }
                if (ch == '"' || ch == '\'') {
                        quote = ch;
                        parser->position++;
                } else if (ch == '(') {
                        parens++;
                        parser->position++;
                } else if (ch == ')') {
                        if (parens == 0 && delimiter == ')') {
                                break;
                        }
                        if (parens == 0) {
                                return false;
                        }
                        parens--;
                        parser->position++;
                } else if (ch == delimiter && parens == 0) {
                        break;
                } else {
                        parser->position++;
                }
        }
        if (quote != 0 || parens != 0 || parser->position == *start) {
                return false;
        }
        *end = parser->position;
        return true;
}

static bool _script_debit(struct executor_script_parser* parser) {
        if (!parser->execute) {
                return true;
        }
        if (parser->context->execution_budget == NULL ||
            *parser->context->execution_budget == 0) {
                return false;
        }
        (*parser->context->execution_budget)--;
        return true;
}

static void _script_pop_locals(struct executor_eval_context* context,
                               size_t count) {
        while (context->local_count > count) {
                context->local_count--;
                free((void*)context->locals[context->local_count].name);
        }
}

static bool _script_name_exists(struct executor_eval_context* context,
                                const uint8_t* name, size_t length) {
        size_t i;

        for (i = 0; i < context->local_count; i++) {
                if (context->locals[i].name_length == length &&
                    memcmp(context->locals[i].name, name, length) == 0) {
                        return true;
                }
        }
        for (i = 0; i < context->binding_count; i++) {
                if (context->bindings[i].name_length == length &&
                    memcmp(context->bindings[i].name, name, length) == 0) {
                        return true;
                }
        }
        for (i = 0; i < context->schema_count; i++) {
                if (context->schema[i].name_length == length &&
                    memcmp(context->schema[i].name, name, length) == 0) {
                        return true;
                }
        }
        return false;
}

static bool _script_set_local(struct executor_eval_context* context,
                              const uint8_t* name, size_t name_length,
                              const struct executor_value* value,
                              bool declare) {
        size_t i;

        for (i = context->local_count; i > 0; i--) {
                struct executor_binding* local = &context->locals[i - 1];
                if (local->name_length == name_length &&
                    memcmp(local->name, name, name_length) == 0) {
                        if (declare ||
                            local->value.type_code != value->type_code ||
                            (value->type_code == EXECUTOR_FORMAT_TYPE_POINTER &&
                             local->value.value.pointer.class_id !=
                                 value->value.pointer.class_id)) {
                                return false;
                        }
                        local->value = *value;
                        return true;
                }
        }
        if (!declare || _script_name_exists(context, name, name_length)) {
                return false;
        }
        {
                size_t capacity = context->local_capacity == 0
                                      ? 8
                                      : context->local_capacity * 2;
                struct executor_binding* grown;
                uint8_t* copy;

                if (capacity > SIZE_MAX / sizeof *context->locals) {
                        return false;
                }
                grown = realloc(context->locals,
                                capacity * sizeof *context->locals);
                if (grown == NULL) {
                        return false;
                }
                context->locals         = grown;
                context->local_capacity = capacity;
                copy = malloc(name_length == 0 ? 1 : name_length);
                if (copy == NULL) {
                        return false;
                }
                memcpy(copy, name, name_length);
                context->locals[context->local_count].name        = copy;
                context->locals[context->local_count].name_length = name_length;
                context->locals[context->local_count].value       = *value;
                context->local_count++;
        }
        return true;
}

static bool _script_check_local_assignment(
    struct executor_eval_context* context, const uint8_t* name,
    size_t name_length, const struct executor_value* value) {
        size_t i;

        for (i = context->local_count; i > 0; i--) {
                const struct executor_binding* local = &context->locals[i - 1];
                if (local->name_length == name_length &&
                    memcmp(local->name, name, name_length) == 0) {
                        return local->value.type_code == value->type_code &&
                               (value->type_code !=
                                    EXECUTOR_FORMAT_TYPE_POINTER ||
                                local->value.value.pointer.class_id ==
                                    value->value.pointer.class_id);
                }
        }
        return false;
}

static bool _script_eval_span(struct executor_script_parser* parser,
                              size_t start, size_t end, bool evaluate,
                              struct executor_value* value) {
        return _evaluate_expression_span(parser->bytes, start, end,
                                         parser->context, evaluate, value);
}

static bool _script_assignment(struct executor_script_parser* parser,
                               bool declaration, size_t end) {
        size_t name_start;
        size_t name_length;
        size_t expression_start;
        size_t expression_end;
        struct executor_value value;
        const uint8_t* name;

        if (!_script_take_word(parser, &name_start, &name_length)) {
                return false;
        }
        name = parser->bytes + name_start;
        _script_skip_space(parser);
        if (parser->position >= end ||
            parser->bytes[parser->position++] != '=') {
                return false;
        }
        if (parser->position < end && parser->bytes[parser->position] == '=') {
                return false;
        }
        if (!_script_read_expression(parser, ';', &expression_start,
                                     &expression_end) ||
            expression_end > end ||
            !_script_eval_span(parser, expression_start, expression_end,
                               parser->execute && !*parser->returned, &value) ||
            !_script_take_delimiter(parser, ';')) {
                return false;
        }
        if (declaration) {
                if (!_script_set_local(parser->context, name, name_length,
                                       &value, true)) {
                        return false;
                }
        } else if (parser->execute && !*parser->returned) {
                if (!_script_set_local(parser->context, name, name_length,
                                       &value, false)) {
                        return false;
                }
        } else if (!_script_check_local_assignment(parser->context, name,
                                                   name_length, &value)) {
                return false;
        }
        return true;
}

static bool _script_clause(struct executor_script_parser* parser, size_t start,
                           size_t end, bool declaration) {
        struct executor_script_parser clause = *parser;
        size_t name_start;
        size_t name_length;
        size_t expression_start;
        struct executor_value value;

        clause.position = start;
        clause.length   = end;
        clause.execute  = parser->execute && !*parser->returned;
        if (declaration) {
                size_t keyword_start;
                size_t keyword_length;
                if (!_script_take_word(&clause, &keyword_start,
                                       &keyword_length) ||
                    !_script_word_is(clause.bytes, keyword_start,
                                     keyword_length, "local")) {
                        return false;
                }
        }
        if (!_script_take_word(&clause, &name_start, &name_length)) {
                return false;
        }
        _script_skip_space(&clause);
        if (clause.position >= end || clause.bytes[clause.position++] != '=') {
                return false;
        }
        expression_start = clause.position;
        if (!_evaluate_expression_span(clause.bytes, expression_start, end,
                                       clause.context, clause.execute,
                                       &value)) {
                return false;
        }
        if (declaration) {
                if (!_script_set_local(clause.context,
                                       clause.bytes + name_start, name_length,
                                       &value, true)) {
                        return false;
                }
        } else if (clause.execute) {
                if (!_script_set_local(clause.context,
                                       clause.bytes + name_start, name_length,
                                       &value, false)) {
                        return false;
                }
        } else if (!_script_check_local_assignment(clause.context,
                                                   clause.bytes + name_start,
                                                   name_length, &value)) {
                return false;
        }
        return true;
}

static bool _script_execute_block(const uint8_t* bytes, size_t start,
                                  size_t end,
                                  struct executor_eval_context* context,
                                  bool execute, bool* returned,
                                  bool* definitely_returns,
                                  struct executor_value* result);

static bool _script_run_statement(struct executor_script_parser* parser) {
        size_t word_start;
        size_t word_length;

        _script_skip_space(parser);
        if (parser->position >= parser->length) {
                return true;
        }
        if (!_script_debit(parser) ||
            !_script_take_word(parser, &word_start, &word_length)) {
                return false;
        }
        if (_script_word_is(parser->bytes, word_start, word_length, "local")) {
                return _script_assignment(parser, true, parser->length);
        }
        if (_script_word_is(parser->bytes, word_start, word_length, "return")) {
                size_t expression_start;
                size_t expression_end;
                struct executor_value value;
                bool evaluate = parser->execute && !*parser->returned;

                if (!_script_read_expression(parser, ';', &expression_start,
                                             &expression_end) ||
                    !_script_eval_span(parser, expression_start, expression_end,
                                       evaluate, &value) ||
                    value.type_code != parser->context->return_type_code ||
                    (value.type_code == EXECUTOR_FORMAT_TYPE_POINTER &&
                     value.value.pointer.class_id !=
                         parser->context->return_target_class_id) ||
                    !_script_take_delimiter(parser, ';')) {
                        return false;
                }
                if (evaluate) {
                        *parser->result   = value;
                        *parser->returned = true;
                }
                *parser->definitely_returns = true;
                return true;
        }
        if (_script_word_is(parser->bytes, word_start, word_length, "if")) {
                size_t condition_start;
                size_t condition_end;
                size_t then_start;
                size_t then_end;
                size_t else_start;
                size_t else_end;
                struct executor_value condition;
                bool execute_then;
                bool execute_else;
                bool evaluate     = parser->execute && !*parser->returned;
                bool then_returns = false;
                bool else_returns = false;
                bool has_else     = false;

                if (!_script_read_group(parser, '(', ')', &condition_start,
                                        &condition_end) ||
                    !_script_eval_span(parser, condition_start, condition_end,
                                       evaluate, &condition) ||
                    condition.type_code != EXECUTOR_FORMAT_TYPE_BOOL ||
                    !_script_read_group(parser, '{', '}', &then_start,
                                        &then_end)) {
                        return false;
                }
                execute_then = evaluate && condition.value.boolean;
                execute_else = evaluate && !condition.value.boolean;
                if (!_script_execute_block(parser->bytes, then_start, then_end,
                                           parser->context, execute_then,
                                           parser->returned, &then_returns,
                                           parser->result)) {
                        return false;
                }
                _script_skip_space(parser);
                {
                        size_t after_then = parser->position;
                        size_t else_word_start;
                        size_t else_word_length;
                        if (_script_take_word(parser, &else_word_start,
                                              &else_word_length) &&
                            _script_word_is(parser->bytes, else_word_start,
                                            else_word_length, "else")) {
                                if (!_script_read_group(parser, '{', '}',
                                                        &else_start,
                                                        &else_end)) {
                                        return false;
                                }
                                has_else = true;
                                if (!_script_execute_block(
                                        parser->bytes, else_start, else_end,
                                        parser->context, execute_else,
                                        parser->returned, &else_returns,
                                        parser->result)) {
                                        return false;
                                }
                        } else {
                                parser->position = after_then;
                        }
                }
                if (has_else && then_returns && else_returns) {
                        *parser->definitely_returns = true;
                }
                return true;
        }
        if (_script_word_is(parser->bytes, word_start, word_length, "while")) {
                size_t condition_start;
                size_t condition_end;
                size_t body_start;
                size_t body_end;
                bool evaluate     = parser->execute && !*parser->returned;
                bool entered      = false;
                bool body_returns = false;

                if (!_script_read_group(parser, '(', ')', &condition_start,
                                        &condition_end) ||
                    !_script_read_group(parser, '{', '}', &body_start,
                                        &body_end)) {
                        return false;
                }
                {
                        struct executor_value condition;
                        if (!_script_eval_span(parser, condition_start,
                                               condition_end, false,
                                               &condition) ||
                            condition.type_code != EXECUTOR_FORMAT_TYPE_BOOL) {
                                return false;
                        }
                }
                while (evaluate) {
                        struct executor_value condition;
                        if (!_script_debit(parser) ||
                            !_script_eval_span(parser, condition_start,
                                               condition_end, true,
                                               &condition) ||
                            condition.type_code != EXECUTOR_FORMAT_TYPE_BOOL) {
                                return false;
                        }
                        if (!condition.value.boolean) {
                                break;
                        }
                        entered = true;
                        if (!_script_execute_block(
                                parser->bytes, body_start, body_end,
                                parser->context, true, parser->returned,
                                &body_returns, parser->result)) {
                                return false;
                        }
                        if (*parser->returned) {
                                evaluate = false;
                                break;
                        }
                }
                if (!entered || !evaluate) {
                        bool body_returned = false;
                        struct executor_value ignored;
                        if (!_script_execute_block(parser->bytes, body_start,
                                                   body_end, parser->context,
                                                   false, &body_returned,
                                                   &body_returns, &ignored)) {
                                return false;
                        }
                }
                return true;
        }
        if (_script_word_is(parser->bytes, word_start, word_length, "for")) {
                size_t header_start;
                size_t header_end;
                size_t body_start;
                size_t body_end;
                size_t first_semicolon  = SIZE_MAX;
                size_t second_semicolon = SIZE_MAX;
                size_t i;
                size_t parens     = 0;
                uint8_t quote     = 0;
                bool escaped      = false;
                size_t scope_base = parser->context->local_count;
                bool evaluate     = parser->execute && !*parser->returned;
                bool entered      = false;
                bool body_returns = false;

                if (!_script_read_group(parser, '(', ')', &header_start,
                                        &header_end)) {
                        return false;
                }
                for (i = header_start; i < header_end; i++) {
                        uint8_t ch = parser->bytes[i];
                        if (quote != 0) {
                                if (escaped) {
                                        escaped = false;
                                } else if (ch == '\\') {
                                        escaped = true;
                                } else if (ch == quote) {
                                        quote = 0;
                                }
                        } else if (ch == '"' || ch == '\'') {
                                quote = ch;
                        } else if (ch == '(') {
                                parens++;
                        } else if (ch == ')' && parens != 0) {
                                parens--;
                        } else if (ch == ';' && parens == 0) {
                                if (first_semicolon == SIZE_MAX) {
                                        first_semicolon = i;
                                } else if (second_semicolon == SIZE_MAX) {
                                        second_semicolon = i;
                                }
                        }
                }
                if (first_semicolon == SIZE_MAX ||
                    second_semicolon == SIZE_MAX ||
                    !_script_read_group(parser, '{', '}', &body_start,
                                        &body_end)) {
                        return false;
                }
                {
                        struct executor_script_parser clauses = *parser;
                        size_t init_start                     = header_start;
                        size_t init_end                       = first_semicolon;
                        size_t kw_start;
                        size_t kw_length;
                        bool declares    = false;
                        clauses.position = init_start;
                        clauses.length   = init_end;
                        if (_script_take_word(&clauses, &kw_start,
                                              &kw_length) &&
                            _script_word_is(clauses.bytes, kw_start, kw_length,
                                            "local")) {
                                declares = true;
                        }
                        if (!_script_clause(parser, init_start, init_end,
                                            declares)) {
                                _script_pop_locals(parser->context, scope_base);
                                return false;
                        }
                }
                {
                        struct executor_value condition;
                        if (!_evaluate_expression_span(
                                parser->bytes, first_semicolon + 1,
                                second_semicolon, parser->context, false,
                                &condition) ||
                            condition.type_code != EXECUTOR_FORMAT_TYPE_BOOL) {
                                _script_pop_locals(parser->context, scope_base);
                                return false;
                        }
                }
                while (evaluate) {
                        struct executor_value condition;
                        if (!_evaluate_expression_span(
                                parser->bytes, first_semicolon + 1,
                                second_semicolon, parser->context, true,
                                &condition) ||
                            condition.type_code != EXECUTOR_FORMAT_TYPE_BOOL) {
                                _script_pop_locals(parser->context, scope_base);
                                return false;
                        }
                        if (!condition.value.boolean) {
                                break;
                        }
                        if (!_script_debit(parser) ||
                            !_script_execute_block(
                                parser->bytes, body_start, body_end,
                                parser->context, true, parser->returned,
                                &body_returns, parser->result)) {
                                _script_pop_locals(parser->context, scope_base);
                                return false;
                        }
                        entered = true;
                        if (*parser->returned) {
                                evaluate = false;
                                break;
                        }
                        {
                                struct executor_script_parser update = *parser;
                                update.position = second_semicolon + 1;
                                update.length   = header_end;
                                if (!_script_clause(&update,
                                                    second_semicolon + 1,
                                                    header_end, false)) {
                                        _script_pop_locals(parser->context,
                                                           scope_base);
                                        return false;
                                }
                        }
                }
                if (!entered || !evaluate) {
                        bool body_returned = false;
                        struct executor_value ignored;
                        struct executor_script_parser update = *parser;
                        update.execute                       = false;
                        update.position = second_semicolon + 1;
                        update.length   = header_end;
                        if (!_script_execute_block(parser->bytes, body_start,
                                                   body_end, parser->context,
                                                   false, &body_returned,
                                                   &body_returns, &ignored) ||
                            !_script_clause(&update, second_semicolon + 1,
                                            header_end, false)) {
                                _script_pop_locals(parser->context, scope_base);
                                return false;
                        }
                }
                _script_pop_locals(parser->context, scope_base);
                return true;
        }
        parser->position = word_start;
        return _script_assignment(parser, false, parser->length);
}

static bool _script_execute_block(const uint8_t* bytes, size_t start,
                                  size_t end,
                                  struct executor_eval_context* context,
                                  bool execute, bool* returned,
                                  bool* definitely_returns,
                                  struct executor_value* result) {
        struct executor_script_parser parser;
        size_t scope_base = context->local_count;
        bool guaranteed   = false;

        if (context->script_depth >= EXECUTOR_FUNCTION_SCRIPT_DEPTH_LIMIT) {
                return false;
        }
        context->script_depth++;
        memset(&parser, 0, sizeof parser);
        parser.bytes              = bytes;
        parser.length             = end;
        parser.position           = start;
        parser.context            = context;
        parser.execute            = execute;
        parser.returned           = returned;
        parser.definitely_returns = NULL;
        parser.result             = result;
        while (parser.position < parser.length) {
                size_t before          = parser.position;
                bool statement_returns = false;
                if (*returned) {
                        parser.execute = false;
                }
                parser.definitely_returns = &statement_returns;
                if (!_script_run_statement(&parser) ||
                    parser.position <= before) {
                        _script_pop_locals(context, scope_base);
                        context->script_depth--;
                        return false;
                }
                if (statement_returns) {
                        guaranteed = true;
                }
                _script_skip_space(&parser);
        }
        _script_pop_locals(context, scope_base);
        context->script_depth--;
        *definitely_returns = guaranteed;
        return true;
}

static bool _evaluate_script_body(const uint8_t* bytes, size_t length,
                                  struct executor_eval_context* context,
                                  struct executor_value* result) {
        struct executor_script_parser parser;
        size_t body_start;
        size_t body_end;
        size_t local_base       = context->local_count;
        bool returned           = false;
        bool definitely_returns = false;
        bool ok;

        memset(&parser, 0, sizeof parser);
        parser.bytes    = bytes;
        parser.length   = length;
        parser.position = 0;
        parser.context  = context;
        if (!_script_read_group(&parser, '{', '}', &body_start, &body_end)) {
                return false;
        }
        _script_skip_space(&parser);
        if (parser.position != length) {
                return false;
        }
        ok = _script_execute_block(bytes, body_start, body_end, context, true,
                                   &returned, &definitely_returns, result);
        _script_pop_locals(context, local_base);
        if (local_base == 0) {
                free(context->locals);
                context->locals         = NULL;
                context->local_capacity = 0;
        }
        return ok && returned && definitely_returns;
}

static bool _evaluate_named_function(struct executor_text_parser* parser,
                                     const uint8_t* name, size_t name_length,
                                     const struct executor_value* arguments,
                                     size_t argument_count, bool evaluate,
                                     struct executor_value* result) {
        struct executor_eval_context* caller = parser->context;
        struct executor_format_function_entry entry;
        struct executor_function_view function;
        struct executor_binding* bindings = NULL;
        struct executor_eval_context nested;
        char* name_text = NULL;
        size_t i;
        bool loaded = false;
        bool ok     = false;

        if (caller == NULL || caller->file == NULL ||
            caller->execution_budget == NULL ||
            *caller->execution_budget == 0 ||
            caller->call_depth >= EXECUTOR_FUNCTION_CALL_DEPTH_LIMIT ||
            name_length == 0) {
                return false;
        }
        name_text = malloc(name_length + 1);
        if (name_text == NULL) {
                return false;
        }
        memcpy(name_text, name, name_length);
        name_text[name_length] = '\0';
        if (!_find_function(caller->file, caller->owner_class_id, name_text,
                            NULL, &entry) ||
            !_load_function_view(caller->file, &entry, &function)) {
                goto done;
        }
        loaded = true;
        if (argument_count != function.parameter_count) {
                goto done;
        }
        bindings =
            calloc(argument_count == 0 ? 1 : argument_count, sizeof *bindings);
        if (bindings == NULL) {
                goto done;
        }
        for (i = 0; i < argument_count; i++) {
                bindings[i].name        = function.parameters[i].name;
                bindings[i].name_length = function.parameters[i].name_length;
                bindings[i].value       = arguments[i];
                if (arguments[i].type_code !=
                        function.parameters[i].type_code ||
                    (arguments[i].type_code == EXECUTOR_FORMAT_TYPE_POINTER &&
                     arguments[i].value.pointer.class_id !=
                         function.parameters[i].target_class_id)) {
                        goto done;
                }
        }
        if (!evaluate) {
                memset(result, 0, sizeof *result);
                result->type_code = function.return_type_code;
                if (result->type_code == EXECUTOR_FORMAT_TYPE_POINTER) {
                        result->value.pointer.class_id =
                            function.return_target_class_id;
                }
                ok = true;
                goto done;
        }
        (*caller->execution_budget)--;
        nested                = *caller;
        nested.bindings       = bindings;
        nested.binding_count  = argument_count;
        nested.locals         = NULL;
        nested.local_count    = 0;
        nested.local_capacity = 0;
        nested.call_depth++;
        nested.return_type_code       = function.return_type_code;
        nested.return_target_class_id = function.return_target_class_id;
        ok =
            _evaluate_text_expression(function.expression,
                                      function.expression_length, &nested,
                                      result) &&
            result->type_code == function.return_type_code &&
            (result->type_code != EXECUTOR_FORMAT_TYPE_POINTER ||
             result->value.pointer.class_id == function.return_target_class_id);
        free(nested.locals);
        nested.locals = NULL;
done:
        free(bindings);
        if (loaded) {
                _function_view_free(&function);
        }
        free(name_text);
        return ok;
}
