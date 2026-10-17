/**
 * @file executor_expression.c
 * @brief Executor expression implementation.
 */

#include "implementation/executor/executor_internal.h"

static bool _parse_text_expression(struct executor_text_parser* parser,
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
                return _resolve_identifier(parser->context,
                                           parser->bytes + start,
                                           parser->position - start, result);
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

static bool _parse_text_unary(struct executor_text_parser* parser,
                              struct executor_value* result) {
        _text_skip_space(parser);
        if (parser->position < parser->length &&
            parser->bytes[parser->position] == '-') {
                struct executor_value operand;
                parser->position++;
                if (!_parse_text_unary(parser, &operand)) {
                        return false;
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
                if (!_parse_text_unary(parser, &right) ||
                    !_apply_text_operator(op, &left, &right, &combined)) {
                        return false;
                }
                left = combined;
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
                if (!_parse_text_term(parser, &right) ||
                    !_apply_text_operator(op, &left, &right, &combined)) {
                        return false;
                }
                left = combined;
        }
        *result = left;
        return true;
}

static bool _parse_text_expression(struct executor_text_parser* parser,
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
        if (!_parse_text_additive(parser, &right) ||
            !_apply_text_operator(op, &left, &right, &combined)) {
                return false;
        }
        _text_skip_space(parser);
        if (parser->position != parser->length) {
                return false;
        }
        *result = combined;
        return true;
}

bool _evaluate_text_expression(const uint8_t* expression,
                               size_t expression_length,
                               struct executor_eval_context* context,
                               struct executor_value* result) {
        struct executor_text_parser parser;
        bool ok;

        memset(&parser, 0, sizeof parser);
        parser.bytes   = expression;
        parser.length  = expression_length;
        parser.context = context;
        ok             = _parse_text_expression(&parser, result);
        if (ok) {
                _text_skip_space(&parser);
                ok = parser.position == parser.length;
        }
        _text_parser_clear(&parser);
        return ok;
}
