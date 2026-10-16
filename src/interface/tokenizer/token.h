#ifndef INTERFACE_TOKENIZER_TOKEN_H
#define INTERFACE_TOKENIZER_TOKEN_H

/**
 * @file token.h
 * @brief Token structures produced by the tokenizer for the analyzer.
 *
 * Every statement form, keyword, type, literal, operator and punctuation
 * mark is its own enumerator. Multi-word phrases that are contiguous in the
 * syntax (CREATE CLASS, LIST CLASSES, ...) are a single token. The tokenizer
 * only classifies; it never converts values. Names and literals carry the
 * text they were written as, so the next stage knows what each token was.
 */

#include <stddef.h>

/**
 * @brief Every kind of token the tokenizer can emit.
 */
enum TOKENIZER_TOKEN {
        /** Zero value, never emitted on success (errors are reported). */
        TOKENIZER_ILLEGAL = 0,
        /** End of input. */
        TOKENIZER_END,
        /** Class, interface, field or function name. */
        TOKENIZER_IDENTIFIER,

        /** CREATE CLASS <n> ... */
        TOKENIZER_CREATE_CLASS,
        /** DROP CLASS <n> */
        TOKENIZER_DROP_CLASS,
        /** ALTER CLASS <n> ... (name follows, then an alter token) */
        TOKENIZER_ALTER_CLASS,
        /** CREATE INTERFACE <n> (...) */
        TOKENIZER_CREATE_INTERFACE,
        /** DROP INTERFACE <n> */
        TOKENIZER_DROP_INTERFACE,
        /** CREATE FUNCTION <class>.<n> (...) RETURNS <type> AS <expr> */
        TOKENIZER_CREATE_FUNCTION,
        /** DROP FUNCTION <class>.<n> */
        TOKENIZER_DROP_FUNCTION,
        /** CALL <class>.<n> (...) ON <id> */
        TOKENIZER_CALL,
        /** NEW <class> (...) */
        TOKENIZER_NEW,
        /** FIND <class> [WHERE <condition>] */
        TOKENIZER_FIND,
        /** SET <class> <id> (...) */
        TOKENIZER_SET,
        /** DELETE <class> <id> */
        TOKENIZER_DELETE,
        /** LIST CLASSES */
        TOKENIZER_LIST_CLASSES,
        /** LIST INTERFACES */
        TOKENIZER_LIST_INTERFACES,
        /** DESCRIBE <class> */
        TOKENIZER_DESCRIBE,

        /** ADD FIELD <field> <type> (after ALTER CLASS <n>) */
        TOKENIZER_ADD_FIELD,
        /** DROP FIELD <field> (after ALTER CLASS <n>) */
        TOKENIZER_DROP_FIELD,
        /** RENAME FIELD <old> <new> (after ALTER CLASS <n>) */
        TOKENIZER_RENAME_FIELD,
        /** RENAME <new> (after ALTER CLASS <n>) */
        TOKENIZER_RENAME,
        /** EXTENDS <class> (CREATE CLASS and ALTER CLASS) */
        TOKENIZER_EXTENDS,
        /** IMPLEMENTS <interface> (CREATE CLASS and ALTER CLASS) */
        TOKENIZER_IMPLEMENTS,
        /** RETURNS <type> */
        TOKENIZER_RETURNS,
        /** AS <expression> */
        TOKENIZER_AS,
        /** ON <id> */
        TOKENIZER_ON,
        /** WHERE <condition> */
        TOKENIZER_WHERE,

        /** integer */
        TOKENIZER_TYPE_INTEGER,
        /** float */
        TOKENIZER_TYPE_FLOAT,
        /** text */
        TOKENIZER_TYPE_TEXT,
        /** bool */
        TOKENIZER_TYPE_BOOL,
        /** pointer */
        TOKENIZER_TYPE_POINTER,

        /** 42 */
        TOKENIZER_LITERAL_INTEGER,
        /** 9.99 */
        TOKENIZER_LITERAL_FLOAT,
        /** "text", lexeme includes the quotes */
        TOKENIZER_LITERAL_TEXT,
        /** true */
        TOKENIZER_LITERAL_TRUE,
        /** false */
        TOKENIZER_LITERAL_FALSE,

        /** = */
        TOKENIZER_EQUAL,
        /** != */
        TOKENIZER_NOT_EQUAL,
        /** < */
        TOKENIZER_LESS,
        /** <= */
        TOKENIZER_LESS_EQUAL,
        /** > */
        TOKENIZER_GREATER,
        /** >= */
        TOKENIZER_GREATER_EQUAL,
        /** + */
        TOKENIZER_PLUS,
        /** - */
        TOKENIZER_MINUS,
        /** * */
        TOKENIZER_STAR,
        /** / */
        TOKENIZER_SLASH,

        /** ( */
        TOKENIZER_LPAREN,
        /** ) */
        TOKENIZER_RPAREN,
        /** , */
        TOKENIZER_COMMA,
        /** . */
        TOKENIZER_DOT,

        /** Number of enumerators, for tables indexed by token. */
        TOKENIZER_COUNT
};

/**
 * @brief Where a token starts in the command line input.
 */
struct tokenizer_position {
        /** Index into argv. */
        size_t argv_index;
        /** Byte offset inside that argv entry. */
        size_t offset;
};

/**
 * @brief A single token.
 */
struct tokenizer_token {
        /** What the token is. */
        enum TOKENIZER_TOKEN type;
        /**
         * Raw source of the token, points into argv (not owned), meant for
         * error messages. For multi-word tokens (CREATE_CLASS, ...) this is
         * the first word only. Use text for the value.
         */
        const char* lexeme;
        /** Length of lexeme in bytes. */
        size_t length;
        /**
         * Owned, NUL-terminated text of the token, NULL when the type says
         * everything. Set for:
         *  - TOKENIZER_IDENTIFIER: the name, e.g. "Person"
         *  - TOKENIZER_LITERAL_TEXT: the content without quotes, backslash
         *    escapes resolved, e.g. Frank "F" Herbert
         *  - TOKENIZER_LITERAL_INTEGER, TOKENIZER_LITERAL_FLOAT: the digits,
         *    e.g. "65" or "9.99"
         * Released by i_tokenizer_free().
         */
        char* text;
        /** Where the token starts. */
        struct tokenizer_position position;
};

/**
 * @brief Growable token array handed to the analyzer.
 */
struct tokenizer_tokens {
        /** Heap array of tokens. */
        struct tokenizer_token* items;
        /** Number of tokens in use. */
        size_t count;
        /** Number of tokens allocated. */
        size_t capacity;
};

#endif// INTERFACE_TOKENIZER_TOKEN_H
