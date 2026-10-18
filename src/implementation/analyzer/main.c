/**
 * @file main.c
 * @brief Analyzer implementation, a recursive descent parser over tokens.
 *
 * Every _parse_* function consumes the tokens it needs. On failure it sets
 * the error, frees what it allocated itself and returns false (or NULL).
 * Whatever was already stored in the statement is released by i_analyze().
 */

#include "interface/analyzer/main.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/** Deepest nesting of parentheses and minus signs in an expression. */
#define MAX_DEPTH 128

/** Parser state. */
struct parser {
        const struct tokenizer_token* tokens;
        size_t position;
        size_t depth;
        struct analyzer_error* error;
};

/** Current token, the last token is always TOKENIZER_END. */
static const struct tokenizer_token* _peek(const struct parser* p) {
        return &p->tokens[p->position];
}

/** Return the current token and step past it, never past TOKENIZER_END. */
static const struct tokenizer_token* _advance(struct parser* p) {
        const struct tokenizer_token* token = &p->tokens[p->position];

        if (token->type != TOKENIZER_END) {
                p->position++;
        }
        return token;
}

/** Step past the current token if it has the given type. */
static bool _accept(struct parser* p, enum TOKENIZER_TOKEN type) {
        if (_peek(p)->type != type) {
                return false;
        }
        _advance(p);
        return true;
}

/** Record an error at the current token, always returns false. */
static bool _fail(struct parser* p, enum analyzer_error_type type,
                  enum TOKENIZER_TOKEN expected) {
        const struct tokenizer_token* token = _peek(p);
        size_t length      = token->lexeme == NULL ? 0 : token->length;
        size_t copy_length = length < sizeof p->error->found_text - 1
                                 ? length
                                 : sizeof p->error->found_text - 1;

        p->error->type              = type;
        p->error->position          = token->position;
        p->error->found             = token->type;
        p->error->expected          = expected;
        p->error->expected_kind     = ANALYZER_EXPECT_NONE;
        p->error->found_text_length = length;
        if (copy_length > 0 && token->lexeme != NULL) {
                memcpy(p->error->found_text, token->lexeme, copy_length);
        }
        p->error->found_text[copy_length] = '\0';
        return false;
}

/** The current token does not fit, expected may be TOKENIZER_ILLEGAL. */
static bool _unexpected(struct parser* p, enum TOKENIZER_TOKEN expected) {
        return _fail(p, ANALYZER_ERROR_UNEXPECTED_TOKEN, expected);
}

/** The current token must be one of a useful group of tokens. */
static bool _unexpected_kind(struct parser* p,
                             enum analyzer_expected_kind expected) {
        _unexpected(p, TOKENIZER_ILLEGAL);
        p->error->expected_kind = expected;
        return false;
}

/** Allocation failed. */
static bool _out_of_memory(struct parser* p) {
        return _fail(p, ANALYZER_ERROR_OUT_OF_MEMORY, TOKENIZER_ILLEGAL);
}

/** Step past a token of the given type or fail. */
static bool _expect(struct parser* p, enum TOKENIZER_TOKEN type) {
        return _accept(p, type) || _unexpected(p, type);
}

/** Copy a string to the heap. */
static char* _dup(const char* text) {
        size_t length = strlen(text);
        char* copy    = malloc(length + 1);

        if (copy != NULL) {
                memcpy(copy, text, length + 1);
        }
        return copy;
}

/** Copy a string to the heap with an optional leading minus sign. */
static char* _dup_signed(const char* text, bool negative) {
        size_t length = strlen(text);
        char* copy    = malloc(length + 2);

        if (copy == NULL) {
                return NULL;
        }
        if (negative) {
                copy[0] = '-';
        }
        memcpy(copy + (negative ? 1 : 0), text, length + 1);
        return copy;
}

/** Copy the text of the current token, which must have the given type. */
static bool _take(struct parser* p, enum TOKENIZER_TOKEN type, char** out) {
        const struct tokenizer_token* token = _peek(p);

        if (token->type != type) {
                return _unexpected(p, type);
        }
        *out = _dup(token->text);
        if (*out == NULL) {
                return _out_of_memory(p);
        }
        _advance(p);
        return true;
}

/** Copy a class, interface, field or function name. */
static bool _take_name(struct parser* p, char** out) {
        return _take(p, TOKENIZER_IDENTIFIER, out);
}

/** Make room for one more item, returns the (new) array or NULL. */
static void* _grow(void* items, size_t* capacity, size_t count,
                   size_t item_size) {
        size_t new_capacity;
        void* grown;

        if (count < *capacity) {
                return items;
        }
        new_capacity = *capacity == 0 ? 4 : *capacity * 2;
        if (new_capacity > SIZE_MAX / item_size) {
                return NULL;
        }
        grown = realloc(items, new_capacity * item_size);
        if (grown == NULL) {
                return NULL;
        }
        *capacity = new_capacity;
        return grown;
}

/** Append a name, on success the list owns it. */
static bool _names_add(struct analyzer_names* list, char* name) {
        void* grown = _grow(list->items, &list->capacity, list->count,
                            sizeof *list->items);

        if (grown == NULL) {
                return false;
        }
        list->items                = grown;
        list->items[list->count++] = name;
        return true;
}

/** Append a field, on success the list owns it. */
static bool _fields_add(struct analyzer_fields* list,
                        struct analyzer_field field) {
        void* grown = _grow(list->items, &list->capacity, list->count,
                            sizeof *list->items);

        if (grown == NULL) {
                return false;
        }
        list->items                = grown;
        list->items[list->count++] = field;
        return true;
}

/** Append a value, on success the list owns it. */
static bool _values_add(struct analyzer_values* list,
                        struct analyzer_value value) {
        void* grown = _grow(list->items, &list->capacity, list->count,
                            sizeof *list->items);

        if (grown == NULL) {
                return false;
        }
        list->items                = grown;
        list->items[list->count++] = value;
        return true;
}

/** Append an assignment, on success the list owns it. */
static bool _assignments_add(struct analyzer_assignments* list,
                             struct analyzer_assignment assignment) {
        void* grown = _grow(list->items, &list->capacity, list->count,
                            sizeof *list->items);

        if (grown == NULL) {
                return false;
        }
        list->items                = grown;
        list->items[list->count++] = assignment;
        return true;
}

/** Release the strings of a value. */
static void _value_free(struct analyzer_value* value) {
        free(value->text);
        free(value->class_name);
        value->text       = NULL;
        value->class_name = NULL;
}

/** Release the strings of a type. */
static void _type_free(struct analyzer_type* type) {
        free(type->class_name);
        type->class_name = NULL;
}

/**
 * Release an expression tree. The left spine is walked in a loop because
 * a chain like 1 + 1 + 1 + ... is as deep as it is long.
 */
static void _expression_free(struct analyzer_expression* node) {
        while (node != NULL) {
                struct analyzer_expression* next = node->left;

                _expression_free(node->right);
                _value_free(&node->value);
                free(node->field);
                free(node);
                node = next;
        }
}

/** Parse a type: integer, float, text, bool or pointer <class>. */
static bool _parse_type(struct parser* p, struct analyzer_type* type) {
        type->class_name = NULL;
        switch (_peek(p)->type) {
                case TOKENIZER_TYPE_INTEGER:
                        type->kind = ANALYZER_TYPE_INTEGER;
                        break;
                case TOKENIZER_TYPE_FLOAT:
                        type->kind = ANALYZER_TYPE_FLOAT;
                        break;
                case TOKENIZER_TYPE_TEXT:
                        type->kind = ANALYZER_TYPE_TEXT;
                        break;
                case TOKENIZER_TYPE_BOOL:
                        type->kind = ANALYZER_TYPE_BOOL;
                        break;
                case TOKENIZER_TYPE_POINTER:
                        type->kind = ANALYZER_TYPE_POINTER;
                        _advance(p);
                        return _take_name(p, &type->class_name);
                default:
                        return _unexpected_kind(p, ANALYZER_EXPECT_TYPE);
        }
        _advance(p);
        return true;
}

/** Parse <name> <type> and append it to the list. */
static bool _parse_field(struct parser* p, struct analyzer_fields* fields) {
        struct analyzer_field field;

        field.name = NULL;
        if (!_take_name(p, &field.name)) {
                return false;
        }
        if (!_parse_type(p, &field.type)) {
                free(field.name);
                return false;
        }
        if (!_fields_add(fields, field)) {
                free(field.name);
                _type_free(&field.type);
                return _out_of_memory(p);
        }
        return true;
}

/** Parse ( <name> <type> [, ...] ). */
static bool _parse_field_list(struct parser* p,
                              struct analyzer_fields* fields) {
        if (!_expect(p, TOKENIZER_LPAREN)) {
                return false;
        }
        do {
                if (!_parse_field(p, fields)) {
                        return false;
                }
        } while (_accept(p, TOKENIZER_COMMA));
        return _expect(p, TOKENIZER_RPAREN);
}

/**
 * Parse a literal: integer, float, text, true or false. A leading minus is
 * only taken in front of a number and only when allow_minus is set.
 */
static bool _parse_literal(struct parser* p, struct analyzer_value* value,
                           bool allow_minus) {
        const struct tokenizer_token* token;
        const char* source;
        bool negative = false;

        value->text       = NULL;
        value->class_name = NULL;
        if (allow_minus && _peek(p)->type == TOKENIZER_MINUS) {
                negative = true;
                _advance(p);
                if (_peek(p)->type != TOKENIZER_LITERAL_INTEGER &&
                    _peek(p)->type != TOKENIZER_LITERAL_FLOAT) {
                        return _unexpected_kind(p, ANALYZER_EXPECT_NUMBER);
                }
        }
        token = _peek(p);
        switch (token->type) {
                case TOKENIZER_LITERAL_INTEGER:
                        value->kind = ANALYZER_VALUE_INTEGER;
                        source      = token->text;
                        break;
                case TOKENIZER_LITERAL_FLOAT:
                        value->kind = ANALYZER_VALUE_FLOAT;
                        source      = token->text;
                        break;
                case TOKENIZER_LITERAL_TEXT:
                        value->kind = ANALYZER_VALUE_TEXT;
                        source      = token->text;
                        break;
                case TOKENIZER_LITERAL_TRUE:
                        value->kind = ANALYZER_VALUE_BOOL;
                        source      = "true";
                        break;
                case TOKENIZER_LITERAL_FALSE:
                        value->kind = ANALYZER_VALUE_BOOL;
                        source      = "false";
                        break;
                default:
                        return _unexpected_kind(p, ANALYZER_EXPECT_LITERAL);
        }
        value->text = _dup_signed(source, negative);
        if (value->text == NULL) {
                return _out_of_memory(p);
        }
        _advance(p);
        return true;
}

/** Parse a pointer value: <class>.<id>. */
static bool _parse_pointer(struct parser* p, struct analyzer_value* value) {
        value->kind       = ANALYZER_VALUE_POINTER;
        value->text       = NULL;
        value->class_name = NULL;
        if (!_take_name(p, &value->class_name) || !_expect(p, TOKENIZER_DOT) ||
            !_take(p, TOKENIZER_LITERAL_INTEGER, &value->text)) {
                _value_free(value);
                return false;
        }
        return true;
}

/** Parse any value: a literal, a negative number or <class>.<id>. */
static bool _parse_value(struct parser* p, struct analyzer_value* value) {
        if (_peek(p)->type == TOKENIZER_IDENTIFIER) {
                return _parse_pointer(p, value);
        }
        return _parse_literal(p, value, true);
}

/** Parse <field> = <value> and append it to the list. */
static bool _parse_assignment(struct parser* p,
                              struct analyzer_assignments* list) {
        struct analyzer_assignment assignment;

        assignment.field = NULL;
        if (!_take_name(p, &assignment.field)) {
                return false;
        }
        if (!_expect(p, TOKENIZER_EQUAL)) {
                free(assignment.field);
                return false;
        }
        if (!_parse_value(p, &assignment.value)) {
                free(assignment.field);
                return false;
        }
        if (!_assignments_add(list, assignment)) {
                free(assignment.field);
                _value_free(&assignment.value);
                return _out_of_memory(p);
        }
        return true;
}

/** Parse ( <field> = <value> [, ...] ). */
static bool _parse_assignment_list(struct parser* p,
                                   struct analyzer_assignments* list) {
        if (!_expect(p, TOKENIZER_LPAREN)) {
                return false;
        }
        do {
                if (!_parse_assignment(p, list)) {
                        return false;
                }
        } while (_accept(p, TOKENIZER_COMMA));
        return _expect(p, TOKENIZER_RPAREN);
}

/** Parse ( <value> [, ...] ). */
static bool _parse_value_list(struct parser* p, struct analyzer_values* list) {
        if (!_expect(p, TOKENIZER_LPAREN)) {
                return false;
        }
        do {
                struct analyzer_value value;

                if (!_parse_value(p, &value)) {
                        return false;
                }
                if (!_values_add(list, value)) {
                        _value_free(&value);
                        return _out_of_memory(p);
                }
        } while (_accept(p, TOKENIZER_COMMA));
        return _expect(p, TOKENIZER_RPAREN);
}

/** Allocate a zeroed expression node. */
static struct analyzer_expression* _node(struct parser* p,
                                         enum ANALYZER_EXPRESSION kind) {
        struct analyzer_expression* node = calloc(1, sizeof *node);

        if (node == NULL) {
                _out_of_memory(p);
                return NULL;
        }
        node->kind = kind;
        return node;
}

/** Build left op right, takes ownership of both operands. */
static struct analyzer_expression* _binary(struct parser* p,
                                           struct analyzer_expression* left,
                                           enum ANALYZER_OPERATOR op,
                                           struct analyzer_expression* right) {
        struct analyzer_expression* node = _node(p, ANALYZER_EXPRESSION_BINARY);

        if (node == NULL) {
                _expression_free(left);
                _expression_free(right);
                return NULL;
        }
        node->op    = op;
        node->left  = left;
        node->right = right;
        return node;
}

static struct analyzer_expression* _parse_additive(struct parser* p);

/** Parse a name, a literal or ( expression ). */
static struct analyzer_expression* _parse_primary(struct parser* p) {
        const struct tokenizer_token* token = _peek(p);
        struct analyzer_expression* node;

        if (token->type == TOKENIZER_LPAREN) {
                _advance(p);
                node = _parse_additive(p);
                if (node != NULL && !_expect(p, TOKENIZER_RPAREN)) {
                        _expression_free(node);
                        return NULL;
                }
                return node;
        }
        if (token->type == TOKENIZER_IDENTIFIER) {
                node = _node(p, ANALYZER_EXPRESSION_FIELD);
                if (node == NULL) {
                        return NULL;
                }
                node->field = _dup(token->text);
                if (node->field == NULL) {
                        free(node);
                        _out_of_memory(p);
                        return NULL;
                }
                _advance(p);
                return node;
        }
        if (token->type != TOKENIZER_LITERAL_INTEGER &&
            token->type != TOKENIZER_LITERAL_FLOAT &&
            token->type != TOKENIZER_LITERAL_TEXT &&
            token->type != TOKENIZER_LITERAL_TRUE &&
            token->type != TOKENIZER_LITERAL_FALSE) {
                _unexpected_kind(p, ANALYZER_EXPECT_EXPRESSION);
                return NULL;
        }
        node = _node(p, ANALYZER_EXPRESSION_LITERAL);
        if (node == NULL) {
                return NULL;
        }
        if (!_parse_literal(p, &node->value, false)) {
                free(node);
                return NULL;
        }
        return node;
}

/** Parse a primary with any number of leading minus signs. */
static struct analyzer_expression* _parse_unary(struct parser* p) {
        struct analyzer_expression* operand;
        struct analyzer_expression* node;

        if (p->depth >= MAX_DEPTH) {
                _fail(p, ANALYZER_ERROR_TOO_DEEP, TOKENIZER_ILLEGAL);
                return NULL;
        }
        p->depth++;
        if (_accept(p, TOKENIZER_MINUS)) {
                operand = _parse_unary(p);
                if (operand == NULL) {
                        p->depth--;
                        return NULL;
                }
                node = _node(p, ANALYZER_EXPRESSION_NEGATE);
                if (node == NULL) {
                        _expression_free(operand);
                } else {
                        node->left = operand;
                }
        } else {
                node = _parse_primary(p);
        }
        p->depth--;
        return node;
}

/** Parse unary [(* | /) unary ...], left associative. */
static struct analyzer_expression* _parse_term(struct parser* p) {
        struct analyzer_expression* left = _parse_unary(p);

        while (left != NULL && (_peek(p)->type == TOKENIZER_STAR ||
                                _peek(p)->type == TOKENIZER_SLASH)) {
                enum ANALYZER_OPERATOR op = _advance(p)->type == TOKENIZER_STAR
                                                ? ANALYZER_OPERATOR_MULTIPLY
                                                : ANALYZER_OPERATOR_DIVIDE;
                struct analyzer_expression* right = _parse_unary(p);

                if (right == NULL) {
                        _expression_free(left);
                        return NULL;
                }
                left = _binary(p, left, op, right);
        }
        return left;
}

/** Parse term [(+ | -) term ...], left associative. */
static struct analyzer_expression* _parse_additive(struct parser* p) {
        struct analyzer_expression* left = _parse_term(p);

        while (left != NULL && (_peek(p)->type == TOKENIZER_PLUS ||
                                _peek(p)->type == TOKENIZER_MINUS)) {
                enum ANALYZER_OPERATOR op = _advance(p)->type == TOKENIZER_PLUS
                                                ? ANALYZER_OPERATOR_ADD
                                                : ANALYZER_OPERATOR_SUBTRACT;
                struct analyzer_expression* right = _parse_term(p);

                if (right == NULL) {
                        _expression_free(left);
                        return NULL;
                }
                left = _binary(p, left, op, right);
        }
        return left;
}

/** Map a comparison token to its operator, false for any other token. */
static bool _comparison_operator(enum TOKENIZER_TOKEN type,
                                 enum ANALYZER_OPERATOR* op) {
        switch (type) {
                case TOKENIZER_EQUAL:
                        *op = ANALYZER_OPERATOR_EQUAL;
                        return true;
                case TOKENIZER_NOT_EQUAL:
                        *op = ANALYZER_OPERATOR_NOT_EQUAL;
                        return true;
                case TOKENIZER_LESS:
                        *op = ANALYZER_OPERATOR_LESS;
                        return true;
                case TOKENIZER_LESS_EQUAL:
                        *op = ANALYZER_OPERATOR_LESS_EQUAL;
                        return true;
                case TOKENIZER_GREATER:
                        *op = ANALYZER_OPERATOR_GREATER;
                        return true;
                case TOKENIZER_GREATER_EQUAL:
                        *op = ANALYZER_OPERATOR_GREATER_EQUAL;
                        return true;
                default:
                        return false;
        }
}

/**
 * Parse additive [comparison additive]. With required set the comparison
 * must be there (a WHERE condition), otherwise it is optional (a function
 * body that returns a number or a bool).
 */
static struct analyzer_expression* _parse_comparison(struct parser* p,
                                                     bool required) {
        struct analyzer_expression* left = _parse_additive(p);
        struct analyzer_expression* right;
        enum ANALYZER_OPERATOR op;

        if (left == NULL) {
                return NULL;
        }
        if (!_comparison_operator(_peek(p)->type, &op)) {
                if (required) {
                        _unexpected_kind(p, ANALYZER_EXPECT_COMPARISON);
                        _expression_free(left);
                        return NULL;
                }
                return left;
        }
        _advance(p);
        right = _parse_additive(p);
        if (right == NULL) {
                _expression_free(left);
                return NULL;
        }
        return _binary(p, left, op, right);
}

/** CREATE CLASS <name> [EXTENDS <class>] [IMPLEMENTS <i>[, ...]] (...) */
static bool _parse_create_class(struct parser* p,
                                struct analyzer_statement* st) {
        if (!_take_name(p, &st->name)) {
                return false;
        }
        if (_accept(p, TOKENIZER_EXTENDS) && !_take_name(p, &st->parent)) {
                return false;
        }
        if (_accept(p, TOKENIZER_IMPLEMENTS)) {
                do {
                        char* name;

                        if (!_take_name(p, &name)) {
                                return false;
                        }
                        if (!_names_add(&st->implements, name)) {
                                free(name);
                                return _out_of_memory(p);
                        }
                } while (_accept(p, TOKENIZER_COMMA));
        }
        return _parse_field_list(p, &st->fields);
}

/** ALTER CLASS <name> followed by one of the alter actions. */
static bool _parse_alter_class(struct parser* p,
                               struct analyzer_statement* st) {
        char* name;

        if (!_take_name(p, &st->name)) {
                return false;
        }
        switch (_peek(p)->type) {
                case TOKENIZER_ADD_FIELD:
                        _advance(p);
                        st->kind = ANALYZER_ALTER_CLASS_ADD_FIELD;
                        return _parse_field(p, &st->fields);
                case TOKENIZER_DROP_FIELD:
                        _advance(p);
                        st->kind = ANALYZER_ALTER_CLASS_DROP_FIELD;
                        return _take_name(p, &st->field);
                case TOKENIZER_RENAME_FIELD:
                        _advance(p);
                        st->kind = ANALYZER_ALTER_CLASS_RENAME_FIELD;
                        return _take_name(p, &st->field) &&
                               _take_name(p, &st->new_name);
                case TOKENIZER_RENAME:
                        _advance(p);
                        st->kind = ANALYZER_ALTER_CLASS_RENAME;
                        return _take_name(p, &st->new_name);
                case TOKENIZER_EXTENDS:
                        _advance(p);
                        st->kind = ANALYZER_ALTER_CLASS_EXTENDS;
                        return _take_name(p, &st->parent);
                case TOKENIZER_IMPLEMENTS:
                        _advance(p);
                        st->kind = ANALYZER_ALTER_CLASS_IMPLEMENTS;
                        if (!_take_name(p, &name)) {
                                return false;
                        }
                        if (!_names_add(&st->implements, name)) {
                                free(name);
                                return _out_of_memory(p);
                        }
                        return true;
                default:
                        return _unexpected_kind(p,
                                                ANALYZER_EXPECT_ALTER_ACTION);
        }
}

/** CREATE FUNCTION <class>.<name> (...) RETURNS <type> AS <expression> */
static bool _parse_create_function(struct parser* p,
                                   struct analyzer_statement* st) {
        if (!_take_name(p, &st->name) || !_expect(p, TOKENIZER_DOT) ||
            !_take_name(p, &st->function) ||
            !_parse_field_list(p, &st->fields) ||
            !_expect(p, TOKENIZER_RETURNS) ||
            !_parse_type(p, &st->return_type) || !_expect(p, TOKENIZER_AS)) {
                return false;
        }
        st->expression = _parse_comparison(p, false);
        return st->expression != NULL;
}

/** CALL <class>.<name> (<value>[, ...]) ON <id> */
static bool _parse_call(struct parser* p, struct analyzer_statement* st) {
        return _take_name(p, &st->name) && _expect(p, TOKENIZER_DOT) &&
               _take_name(p, &st->function) &&
               _parse_value_list(p, &st->values) && _expect(p, TOKENIZER_ON) &&
               _take(p, TOKENIZER_LITERAL_INTEGER, &st->id);
}

/** FIND <class> [WHERE <condition>] */
static bool _parse_find(struct parser* p, struct analyzer_statement* st) {
        if (!_take_name(p, &st->name)) {
                return false;
        }
        if (!_accept(p, TOKENIZER_WHERE)) {
                return true;
        }
        st->expression = _parse_comparison(p, true);
        return st->expression != NULL;
}

/** Parse the whole statement and set st->kind. */
static bool _parse_statement(struct parser* p, struct analyzer_statement* st) {
        enum TOKENIZER_TOKEN head = _peek(p)->type;

        switch (head) {
                case TOKENIZER_CREATE_CLASS:
                case TOKENIZER_DROP_CLASS:
                case TOKENIZER_ALTER_CLASS:
                case TOKENIZER_CREATE_INTERFACE:
                case TOKENIZER_DROP_INTERFACE:
                case TOKENIZER_CREATE_FUNCTION:
                case TOKENIZER_DROP_FUNCTION:
                case TOKENIZER_CALL:
                case TOKENIZER_NEW:
                case TOKENIZER_FIND:
                case TOKENIZER_SET:
                case TOKENIZER_DELETE:
                case TOKENIZER_LIST_CLASSES:
                case TOKENIZER_LIST_INTERFACES:
                case TOKENIZER_DESCRIBE:
                        _advance(p);
                        break;
                default:
                        return _unexpected_kind(p, ANALYZER_EXPECT_STATEMENT);
        }
        switch (head) {
                case TOKENIZER_CREATE_CLASS:
                        st->kind = ANALYZER_CREATE_CLASS;
                        return _parse_create_class(p, st);
                case TOKENIZER_DROP_CLASS:
                        st->kind = ANALYZER_DROP_CLASS;
                        return _take_name(p, &st->name);
                case TOKENIZER_ALTER_CLASS:
                        return _parse_alter_class(p, st);
                case TOKENIZER_CREATE_INTERFACE:
                        st->kind = ANALYZER_CREATE_INTERFACE;
                        return _take_name(p, &st->name) &&
                               _parse_field_list(p, &st->fields);
                case TOKENIZER_DROP_INTERFACE:
                        st->kind = ANALYZER_DROP_INTERFACE;
                        return _take_name(p, &st->name);
                case TOKENIZER_CREATE_FUNCTION:
                        st->kind = ANALYZER_CREATE_FUNCTION;
                        return _parse_create_function(p, st);
                case TOKENIZER_DROP_FUNCTION:
                        st->kind = ANALYZER_DROP_FUNCTION;
                        return _take_name(p, &st->name) &&
                               _expect(p, TOKENIZER_DOT) &&
                               _take_name(p, &st->function);
                case TOKENIZER_CALL:
                        st->kind = ANALYZER_CALL;
                        return _parse_call(p, st);
                case TOKENIZER_NEW:
                        st->kind = ANALYZER_NEW;
                        return _take_name(p, &st->name) &&
                               _parse_assignment_list(p, &st->assignments);
                case TOKENIZER_FIND:
                        st->kind = ANALYZER_FIND;
                        return _parse_find(p, st);
                case TOKENIZER_SET:
                        st->kind = ANALYZER_SET;
                        return _take_name(p, &st->name) &&
                               _take(p, TOKENIZER_LITERAL_INTEGER, &st->id) &&
                               _parse_assignment_list(p, &st->assignments);
                case TOKENIZER_DELETE:
                        st->kind = ANALYZER_DELETE;
                        return _take_name(p, &st->name) &&
                               _take(p, TOKENIZER_LITERAL_INTEGER, &st->id);
                case TOKENIZER_LIST_CLASSES:
                        st->kind = ANALYZER_LIST_CLASSES;
                        return true;
                case TOKENIZER_LIST_INTERFACES:
                        st->kind = ANALYZER_LIST_INTERFACES;
                        return true;
                default:
                        st->kind = ANALYZER_DESCRIBE;
                        return _take_name(p, &st->name);
        }
}

/** Family of a statement kind. */
static enum ANALYZER_GROUP _group_of(enum ANALYZER_STATEMENT kind) {
        switch (kind) {
                case ANALYZER_CREATE_CLASS:
                case ANALYZER_DROP_CLASS:
                case ANALYZER_ALTER_CLASS_ADD_FIELD:
                case ANALYZER_ALTER_CLASS_DROP_FIELD:
                case ANALYZER_ALTER_CLASS_RENAME_FIELD:
                case ANALYZER_ALTER_CLASS_RENAME:
                case ANALYZER_ALTER_CLASS_EXTENDS:
                case ANALYZER_ALTER_CLASS_IMPLEMENTS:
                case ANALYZER_CREATE_INTERFACE:
                case ANALYZER_DROP_INTERFACE:
                        return ANALYZER_GROUP_SCHEMA;
                case ANALYZER_CREATE_FUNCTION:
                case ANALYZER_DROP_FUNCTION:
                case ANALYZER_CALL:
                        return ANALYZER_GROUP_FUNCTION;
                case ANALYZER_NEW:
                case ANALYZER_FIND:
                case ANALYZER_SET:
                case ANALYZER_DELETE:
                        return ANALYZER_GROUP_OBJECT;
                case ANALYZER_LIST_CLASSES:
                case ANALYZER_LIST_INTERFACES:
                case ANALYZER_DESCRIBE:
                        return ANALYZER_GROUP_METADATA;
                default:
                        return ANALYZER_GROUP_NONE;
        }
}

struct analyzer_statement i_analyze(const struct tokenizer_tokens* tokens,
                                    struct analyzer_error* error) {
        struct analyzer_statement statement = {ANALYZER_INVALID};
        struct parser parser;
        bool ok;

        error->type                = ANALYZER_OK;
        error->position.argv_index = 0;
        error->position.offset     = 0;
        error->found               = TOKENIZER_END;
        error->expected            = TOKENIZER_ILLEGAL;
        error->expected_kind       = ANALYZER_EXPECT_NONE;
        error->found_text[0]       = '\0';
        error->found_text_length   = 0;

        if (tokens->count < 2 ||
            tokens->items[tokens->count - 1].type != TOKENIZER_END) {
                error->type = ANALYZER_ERROR_EMPTY;
                return statement;
        }
        parser.tokens   = tokens->items;
        parser.position = 0;
        parser.depth    = 0;
        parser.error    = error;

        ok = _parse_statement(&parser, &statement) &&
             _expect(&parser, TOKENIZER_END);
        if (!ok) {
                i_analyzer_free(&statement);
        } else {
                statement.group = _group_of(statement.kind);
        }
        return statement;
}

void i_analyzer_free(struct analyzer_statement* statement) {
        size_t i;
        struct analyzer_statement empty = {ANALYZER_INVALID};

        free(statement->name);
        free(statement->function);
        free(statement->field);
        free(statement->new_name);
        free(statement->parent);
        free(statement->id);
        for (i = 0; i < statement->implements.count; i++) {
                free(statement->implements.items[i]);
        }
        free(statement->implements.items);
        for (i = 0; i < statement->fields.count; i++) {
                free(statement->fields.items[i].name);
                _type_free(&statement->fields.items[i].type);
        }
        free(statement->fields.items);
        _type_free(&statement->return_type);
        for (i = 0; i < statement->values.count; i++) {
                _value_free(&statement->values.items[i]);
        }
        free(statement->values.items);
        for (i = 0; i < statement->assignments.count; i++) {
                free(statement->assignments.items[i].field);
                _value_free(&statement->assignments.items[i].value);
        }
        free(statement->assignments.items);
        _expression_free(statement->expression);
        *statement = empty;
}
