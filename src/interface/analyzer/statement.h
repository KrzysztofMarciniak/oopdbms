#ifndef INTERFACE_ANALYZER_STATEMENT_H
#define INTERFACE_ANALYZER_STATEMENT_H

/**
 * @file statement.h
 * @brief The checked statement the analyzer hands to the next stage.
 *
 * A statement owns all of its strings, it does not point into the tokens or
 * into argv. Fields that a statement kind does not use are NULL or empty.
 */

#include <stddef.h>

/**
 * @brief Which statement was written.
 */
enum ANALYZER_STATEMENT {
        /** Zero value, no statement. */
        ANALYZER_INVALID = 0,
        /** CREATE CLASS: name, parent, implements, fields */
        ANALYZER_CREATE_CLASS,
        /** DROP CLASS: name */
        ANALYZER_DROP_CLASS,
        /** ALTER CLASS ADD FIELD: name, fields (exactly one) */
        ANALYZER_ALTER_CLASS_ADD_FIELD,
        /** ALTER CLASS DROP FIELD: name, field */
        ANALYZER_ALTER_CLASS_DROP_FIELD,
        /** ALTER CLASS RENAME FIELD: name, field (old), new_name */
        ANALYZER_ALTER_CLASS_RENAME_FIELD,
        /** ALTER CLASS RENAME: name, new_name */
        ANALYZER_ALTER_CLASS_RENAME,
        /** ALTER CLASS EXTENDS: name, parent */
        ANALYZER_ALTER_CLASS_EXTENDS,
        /** ALTER CLASS IMPLEMENTS: name, implements (exactly one) */
        ANALYZER_ALTER_CLASS_IMPLEMENTS,
        /** CREATE INTERFACE: name, fields */
        ANALYZER_CREATE_INTERFACE,
        /** DROP INTERFACE: name */
        ANALYZER_DROP_INTERFACE,
        /** CREATE FUNCTION: name (class), function, fields (arguments),
            return_type, expression */
        ANALYZER_CREATE_FUNCTION,
        /** DROP FUNCTION: name (class), function */
        ANALYZER_DROP_FUNCTION,
        /** CALL: name (class), function, values, id */
        ANALYZER_CALL,
        /** NEW: name (class), assignments */
        ANALYZER_NEW,
        /** FIND: name (class), expression (condition, NULL without WHERE) */
        ANALYZER_FIND,
        /** SET: name (class), id, assignments */
        ANALYZER_SET,
        /** DELETE: name (class), id */
        ANALYZER_DELETE,
        /** LIST CLASSES: nothing */
        ANALYZER_LIST_CLASSES,
        /** LIST INTERFACES: nothing */
        ANALYZER_LIST_INTERFACES,
        /** DESCRIBE: name (class) */
        ANALYZER_DESCRIBE
};

/**
 * @brief Family of a statement, what the executor routes on first.
 */
enum ANALYZER_GROUP {
        /** Zero value, no statement. */
        ANALYZER_GROUP_NONE = 0,
        /** CREATE/DROP CLASS, CREATE/DROP INTERFACE, ALTER CLASS ... */
        ANALYZER_GROUP_SCHEMA,
        /** CREATE/DROP FUNCTION, CALL */
        ANALYZER_GROUP_FUNCTION,
        /** NEW, FIND, SET, DELETE */
        ANALYZER_GROUP_OBJECT,
        /** LIST CLASSES, LIST INTERFACES, DESCRIBE */
        ANALYZER_GROUP_METADATA
};

/**
 * @brief Kind of a declared type.
 */
enum ANALYZER_TYPE {
        /** integer */
        ANALYZER_TYPE_INTEGER = 0,
        /** float */
        ANALYZER_TYPE_FLOAT,
        /** text */
        ANALYZER_TYPE_TEXT,
        /** bool */
        ANALYZER_TYPE_BOOL,
        /** pointer <class> */
        ANALYZER_TYPE_POINTER
};

/**
 * @brief A declared type.
 */
struct analyzer_type {
        /** Which type. */
        enum ANALYZER_TYPE kind;
        /** Target class, only for ANALYZER_TYPE_POINTER. */
        char* class_name;
};

/**
 * @brief A field or argument declaration: <name> <type>.
 */
struct analyzer_field {
        /** Field or argument name. */
        char* name;
        /** Declared type. */
        struct analyzer_type type;
};

/**
 * @brief Growable list of field declarations.
 */
struct analyzer_fields {
        /** Heap array. */
        struct analyzer_field* items;
        /** Entries in use. */
        size_t count;
        /** Entries allocated. */
        size_t capacity;
};

/**
 * @brief Growable list of names.
 */
struct analyzer_names {
        /** Heap array of heap strings. */
        char** items;
        /** Entries in use. */
        size_t count;
        /** Entries allocated. */
        size_t capacity;
};

/**
 * @brief Kind of a written value.
 */
enum ANALYZER_VALUE {
        /** 42 or -5 */
        ANALYZER_VALUE_INTEGER = 0,
        /** 9.99 or -0.5 */
        ANALYZER_VALUE_FLOAT,
        /** "text" */
        ANALYZER_VALUE_TEXT,
        /** true or false */
        ANALYZER_VALUE_BOOL,
        /** <class>.<id> */
        ANALYZER_VALUE_POINTER
};

/**
 * @brief A written value.
 */
struct analyzer_value {
        /** Which kind of value. */
        enum ANALYZER_VALUE kind;
        /**
         * The value as written: digits (with a leading - when negative),
         * the text content without quotes, "true" or "false", or the id of
         * a pointer. Numbers are not converted.
         */
        char* text;
        /** Target class, only for ANALYZER_VALUE_POINTER. */
        char* class_name;
};

/**
 * @brief Growable list of values.
 */
struct analyzer_values {
        /** Heap array. */
        struct analyzer_value* items;
        /** Entries in use. */
        size_t count;
        /** Entries allocated. */
        size_t capacity;
};

/**
 * @brief One <field> = <value> pair.
 */
struct analyzer_assignment {
        /** Field name. */
        char* field;
        /** Value to store. */
        struct analyzer_value value;
};

/**
 * @brief Growable list of assignments.
 */
struct analyzer_assignments {
        /** Heap array. */
        struct analyzer_assignment* items;
        /** Entries in use. */
        size_t count;
        /** Entries allocated. */
        size_t capacity;
};

/**
 * @brief Operator inside an expression or condition.
 */
enum ANALYZER_OPERATOR {
        /** + */
        ANALYZER_OPERATOR_ADD = 0,
        /** - */
        ANALYZER_OPERATOR_SUBTRACT,
        /** * */
        ANALYZER_OPERATOR_MULTIPLY,
        /** / */
        ANALYZER_OPERATOR_DIVIDE,
        /** = */
        ANALYZER_OPERATOR_EQUAL,
        /** != */
        ANALYZER_OPERATOR_NOT_EQUAL,
        /** < */
        ANALYZER_OPERATOR_LESS,
        /** <= */
        ANALYZER_OPERATOR_LESS_EQUAL,
        /** > */
        ANALYZER_OPERATOR_GREATER,
        /** >= */
        ANALYZER_OPERATOR_GREATER_EQUAL
};

/**
 * @brief Kind of an expression node.
 */
enum ANALYZER_EXPRESSION {
        /** A written value, see value. */
        ANALYZER_EXPRESSION_LITERAL = 0,
        /** A field or argument name, see field. */
        ANALYZER_EXPRESSION_FIELD,
        /** Unary minus, operand in left. */
        ANALYZER_EXPRESSION_NEGATE,
        /** left op right, see op. */
        ANALYZER_EXPRESSION_BINARY
};

/**
 * @brief Node of an expression tree.
 */
struct analyzer_expression {
        /** What the node is. */
        enum ANALYZER_EXPRESSION kind;
        /** Operator, only for ANALYZER_EXPRESSION_BINARY. */
        enum ANALYZER_OPERATOR op;
        /** Only for ANALYZER_EXPRESSION_LITERAL. */
        struct analyzer_value value;
        /** Name, only for ANALYZER_EXPRESSION_FIELD. */
        char* field;
        /** Left operand (BINARY) or the operand (NEGATE), else NULL. */
        struct analyzer_expression* left;
        /** Right operand, only for BINARY. */
        struct analyzer_expression* right;
};

/**
 * @brief A fully checked statement.
 */
struct analyzer_statement {
        /** Which statement, selects the fields below that are used. */
        enum ANALYZER_STATEMENT kind;
        /** Family of kind, set together with it. */
        enum ANALYZER_GROUP group;
        /** Class or interface the statement is about. */
        char* name;
        /** Function name (CREATE/DROP FUNCTION, CALL). */
        char* function;
        /** Field to drop, or the old name when renaming a field. */
        char* field;
        /** New class name or new field name. */
        char* new_name;
        /** Class named by EXTENDS. */
        char* parent;
        /** Object id (CALL ... ON, SET, DELETE), digits as written. */
        char* id;
        /** Interfaces named by IMPLEMENTS. */
        struct analyzer_names implements;
        /** Declared fields, or function arguments. */
        struct analyzer_fields fields;
        /** Declared return type of a function. */
        struct analyzer_type return_type;
        /** Argument values of CALL. */
        struct analyzer_values values;
        /** Pairs of NEW and SET. */
        struct analyzer_assignments assignments;
        /** Function body, or FIND condition (NULL without WHERE). */
        struct analyzer_expression* expression;
};

#endif// INTERFACE_ANALYZER_STATEMENT_H
