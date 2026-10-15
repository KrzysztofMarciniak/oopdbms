/**
 * @file main.c
 * @brief Tokenizer implementation.
 *
 * Two stages:
 *  1. _split_entry() cuts every argv entry into pieces, bash-like.
 *  2. _classify() turns pieces into tokens and fuses phrases such as
 *     CREATE CLASS into one token.
 */

#include "interface/tokenizer/main.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/** Kind of a raw piece produced by the split stage. */
enum piece_kind {
        PIECE_WORD,
        PIECE_INTEGER,
        PIECE_FLOAT,
        PIECE_TEXT,
        PIECE_SYMBOL
};

/** A raw piece of input, not yet classified. */
struct piece {
        enum piece_kind kind;
        const char* start;
        size_t length;
        struct tokenizer_position position;
};

/** Growable piece array. */
struct pieces {
        struct piece* items;
        size_t count;
        size_t capacity;
};

/** Two-word phrase that becomes one token. */
struct phrase {
        const char* first;
        const char* second;
        enum TOKENIZER_TOKEN type;
};

/** One-word keyword. */
struct keyword {
        const char* text;
        enum TOKENIZER_TOKEN type;
};

static const struct phrase PHRASES[] = {
    {"CREATE", "CLASS", TOKENIZER_CREATE_CLASS},
    {"DROP", "CLASS", TOKENIZER_DROP_CLASS},
    {"ALTER", "CLASS", TOKENIZER_ALTER_CLASS},
    {"CREATE", "INTERFACE", TOKENIZER_CREATE_INTERFACE},
    {"DROP", "INTERFACE", TOKENIZER_DROP_INTERFACE},
    {"CREATE", "FUNCTION", TOKENIZER_CREATE_FUNCTION},
    {"DROP", "FUNCTION", TOKENIZER_DROP_FUNCTION},
    {"LIST", "CLASSES", TOKENIZER_LIST_CLASSES},
    {"LIST", "INTERFACES", TOKENIZER_LIST_INTERFACES},
    {"ADD", "FIELD", TOKENIZER_ADD_FIELD},
    {"DROP", "FIELD", TOKENIZER_DROP_FIELD},
    {"RENAME", "FIELD", TOKENIZER_RENAME_FIELD},
};

static const struct keyword KEYWORDS[] = {
    {"CALL", TOKENIZER_CALL},
    {"NEW", TOKENIZER_NEW},
    {"FIND", TOKENIZER_FIND},
    {"SET", TOKENIZER_SET},
    {"DELETE", TOKENIZER_DELETE},
    {"DESCRIBE", TOKENIZER_DESCRIBE},
    {"RENAME", TOKENIZER_RENAME},
    {"EXTENDS", TOKENIZER_EXTENDS},
    {"IMPLEMENTS", TOKENIZER_IMPLEMENTS},
    {"RETURNS", TOKENIZER_RETURNS},
    {"AS", TOKENIZER_AS},
    {"ON", TOKENIZER_ON},
    {"WHERE", TOKENIZER_WHERE},
    {"integer", TOKENIZER_TYPE_INTEGER},
    {"float", TOKENIZER_TYPE_FLOAT},
    {"text", TOKENIZER_TYPE_TEXT},
    {"bool", TOKENIZER_TYPE_BOOL},
    {"pointer", TOKENIZER_TYPE_POINTER},
    {"true", TOKENIZER_LITERAL_TRUE},
    {"false", TOKENIZER_LITERAL_FALSE},
};

/** Words that only mean something together with a second word. */
static const char* const PHRASE_HEADS[] = {"CREATE", "DROP", "ALTER", "LIST",
                                           "ADD"};

#define COUNT_OF(array) (sizeof(array) / sizeof((array)[0]))

static bool _is_space(char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
               c == '\v';
}

static bool _is_digit(char c) { return c >= '0' && c <= '9'; }

static bool _is_word_start(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool _is_word_char(char c) { return _is_word_start(c) || _is_digit(c); }

static bool _fail(struct tokenizer_error* error,
                  enum tokenizer_error_type type,
                  struct tokenizer_position position) {
        error->type = type;
        error->position = position;
        return false;
}

/** Make room for one more item, returns the (new) array or NULL. */
static void* _array_grow(void* items, size_t* capacity, size_t count,
                         size_t item_size) {
        size_t new_capacity;
        void* grown;

        if (count < *capacity) {
                return items;
        }
        new_capacity = *capacity == 0 ? 16 : *capacity * 2;
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

static bool _piece_add(struct pieces* pieces, struct piece piece) {
        void* grown = _array_grow(pieces->items, &pieces->capacity,
                                  pieces->count, sizeof *pieces->items);

        if (grown == NULL) {
                return false;
        }
        pieces->items = grown;
        pieces->items[pieces->count++] = piece;
        return true;
}

static bool _token_add(struct tokenizer_tokens* tokens,
                       struct tokenizer_token token) {
        void* grown = _array_grow(tokens->items, &tokens->capacity,
                                  tokens->count, sizeof *tokens->items);

        if (grown == NULL) {
                return false;
        }
        tokens->items = grown;
        tokens->items[tokens->count++] = token;
        return true;
}

static bool _piece_is(const struct piece* piece, const char* text) {
        size_t length = strlen(text);

        return piece->length == length && memcmp(piece->start, text, length) == 0;
}

/** True if the last piece is a '.' that ends exactly at offset. */
static bool _follows_dot(const struct pieces* pieces, size_t argv_index,
                         size_t offset) {
        const struct piece* last;

        if (pieces->count == 0) {
                return false;
        }
        last = &pieces->items[pieces->count - 1];
        return last->kind == PIECE_SYMBOL && last->length == 1 &&
               last->start[0] == '.' &&
               last->position.argv_index == argv_index &&
               last->position.offset + 1 == offset;
}

/**
 * Length of the quoted text starting at text[0] (quote included), 0 if the
 * quote is never closed.
 */
static size_t _scan_text(const char* text) {
        char quote = text[0];
        size_t i = 1;

        while (text[i] != '\0' && text[i] != quote) {
                if (text[i] == '\\' && text[i + 1] != '\0') {
                        i++;
                }
                i++;
        }
        return text[i] == quote ? i + 1 : 0;
}

/** Length of the symbol starting at text[0], 0 if it is not a symbol. */
static size_t _scan_symbol(const char* text) {
        switch (text[0]) {
                case '(':
                case ')':
                case ',':
                case '.':
                case '=':
                case '+':
                case '-':
                case '*':
                case '/':
                        return 1;
                case '<':
                case '>':
                        return text[1] == '=' ? 2 : 1;
                case '!':
                        return text[1] == '=' ? 2 : 0;
                default:
                        return 0;
        }
}

/** Stage 1: cut one argv entry into pieces. */
static bool _split_entry(const char* text, size_t argv_index,
                        struct pieces* pieces, struct tokenizer_error* error) {
        size_t i = 0;

        while (text[i] != '\0') {
                struct piece piece;
                size_t start = i;
                size_t length;

                if (_is_space(text[i])) {
                        i++;
                        continue;
                }
                piece.position.argv_index = argv_index;
                piece.position.offset = start;

                if (_is_word_start(text[i])) {
                        piece.kind = PIECE_WORD;
                        while (_is_word_char(text[i])) {
                                i++;
                        }
                } else if (_is_digit(text[i])) {
                        piece.kind = PIECE_INTEGER;
                        while (_is_digit(text[i])) {
                                i++;
                        }
                        if (text[i] == '.' && _is_digit(text[i + 1]) &&
                            !_follows_dot(pieces, argv_index, start)) {
                                piece.kind = PIECE_FLOAT;
                                i++;
                                while (_is_digit(text[i])) {
                                        i++;
                                }
                        }
                        if (_is_word_char(text[i])) {
                                return _fail(error, TOKENIZER_ERROR_BAD_NUMBER,
                                             piece.position);
                        }
                } else if (text[i] == '"' || text[i] == '\'') {
                        length = _scan_text(text + i);
                        if (length == 0) {
                                return _fail(error,
                                             TOKENIZER_ERROR_UNTERMINATED_TEXT,
                                             piece.position);
                        }
                        piece.kind = PIECE_TEXT;
                        i += length;
                } else {
                        length = _scan_symbol(text + i);
                        if (length == 0) {
                                return _fail(error,
                                             TOKENIZER_ERROR_ILLEGAL_CHARACTER,
                                             piece.position);
                        }
                        piece.kind = PIECE_SYMBOL;
                        i += length;
                }
                piece.start = text + start;
                piece.length = i - start;
                if (!_piece_add(pieces, piece)) {
                        return _fail(error, TOKENIZER_ERROR_OUT_OF_MEMORY,
                                     piece.position);
                }
        }
        return true;
}

static enum TOKENIZER_TOKEN _classify_symbol(const struct piece* piece) {
        switch (piece->start[0]) {
                case '(':
                        return TOKENIZER_LPAREN;
                case ')':
                        return TOKENIZER_RPAREN;
                case ',':
                        return TOKENIZER_COMMA;
                case '.':
                        return TOKENIZER_DOT;
                case '=':
                        return TOKENIZER_EQUAL;
                case '+':
                        return TOKENIZER_PLUS;
                case '-':
                        return TOKENIZER_MINUS;
                case '*':
                        return TOKENIZER_STAR;
                case '/':
                        return TOKENIZER_SLASH;
                case '<':
                        return piece->length == 2 ? TOKENIZER_LESS_EQUAL
                                                  : TOKENIZER_LESS;
                case '>':
                        return piece->length == 2 ? TOKENIZER_GREATER_EQUAL
                                                  : TOKENIZER_GREATER;
                default:
                        return TOKENIZER_NOT_EQUAL;
        }
}

/**
 * Classify a word. Sets *consumed to the number of pieces used (2 for a
 * phrase). Returns TOKENIZER_ILLEGAL for a phrase head without a partner.
 */
static enum TOKENIZER_TOKEN _classify_word(const struct piece* piece,
                                           const struct piece* next,
                                           size_t* consumed) {
        size_t i;

        *consumed = 1;
        if (next != NULL && next->kind == PIECE_WORD) {
                for (i = 0; i < COUNT_OF(PHRASES); i++) {
                        if (_piece_is(piece, PHRASES[i].first) &&
                            _piece_is(next, PHRASES[i].second)) {
                                *consumed = 2;
                                return PHRASES[i].type;
                        }
                }
        }
        for (i = 0; i < COUNT_OF(KEYWORDS); i++) {
                if (_piece_is(piece, KEYWORDS[i].text)) {
                        return KEYWORDS[i].type;
                }
        }
        for (i = 0; i < COUNT_OF(PHRASE_HEADS); i++) {
                if (_piece_is(piece, PHRASE_HEADS[i])) {
                        return TOKENIZER_ILLEGAL;
                }
        }
        return TOKENIZER_IDENTIFIER;
}

/** Stage 2: classify pieces, fuse phrases, append TOKENIZER_END. */
static bool _classify(const struct pieces* pieces, size_t end_index,
                      struct tokenizer_tokens* tokens,
                      struct tokenizer_error* error) {
        struct tokenizer_token token;
        size_t i = 0;
        size_t consumed;

        while (i < pieces->count) {
                const struct piece* piece = &pieces->items[i];
                const struct piece* next =
                    i + 1 < pieces->count ? &pieces->items[i + 1] : NULL;

                consumed = 1;
                switch (piece->kind) {
                        case PIECE_WORD:
                                token.type = _classify_word(piece, next,
                                                            &consumed);
                                break;
                        case PIECE_INTEGER:
                                token.type = TOKENIZER_LITERAL_INTEGER;
                                break;
                        case PIECE_FLOAT:
                                token.type = TOKENIZER_LITERAL_FLOAT;
                                break;
                        case PIECE_TEXT:
                                token.type = TOKENIZER_LITERAL_TEXT;
                                break;
                        default:
                                token.type = _classify_symbol(piece);
                                break;
                }
                if (token.type == TOKENIZER_ILLEGAL) {
                        return _fail(error, TOKENIZER_ERROR_INCOMPLETE_PHRASE,
                                     piece->position);
                }
                token.lexeme = piece->start;
                token.length = piece->length;
                token.position = piece->position;
                if (!_token_add(tokens, token)) {
                        return _fail(error, TOKENIZER_ERROR_OUT_OF_MEMORY,
                                     piece->position);
                }
                i += consumed;
        }
        token.type = TOKENIZER_END;
        token.lexeme = "";
        token.length = 0;
        token.position.argv_index = end_index;
        token.position.offset = 0;
        if (!_token_add(tokens, token)) {
                return _fail(error, TOKENIZER_ERROR_OUT_OF_MEMORY,
                             token.position);
        }
        return true;
}

struct tokenizer_tokens i_tokenize(int argc, char** argv,
                                   struct tokenizer_error* error) {
        struct tokenizer_tokens tokens = {NULL, 0, 0};
        struct pieces pieces = {NULL, 0, 0};
        bool ok = true;
        int i;

        error->type = TOKENIZER_OK;
        error->position.argv_index = 0;
        error->position.offset = 0;

        for (i = 1; ok && i < argc; i++) {
                ok = _split_entry(argv[i], (size_t) i, &pieces, error);
        }
        if (ok) {
                ok = _classify(&pieces, argc > 0 ? (size_t) argc : 0, &tokens,
                               error);
        }
        free(pieces.items);
        if (!ok) {
                i_tokenizer_free(&tokens);
        }
        return tokens;
}

void i_tokenizer_free(struct tokenizer_tokens* tokens) {
        free(tokens->items);
        tokens->items = NULL;
        tokens->count = 0;
        tokens->capacity = 0;
}
