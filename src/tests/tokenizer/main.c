/**
 * @file main.c
 * @brief Tokenizer unit tests, run with `make test`.
 */

#include "interface/tokenizer/main.h"

#include <stdio.h>
#include <string.h>

static int checks   = 0;
static int failures = 0;

#define CHECK(condition, name)                                         \
        do {                                                           \
                checks++;                                              \
                if (!(condition)) {                                    \
                        failures++;                                    \
                        printf("FAIL: %s (%s:%d)\n", (name), __FILE__, \
                               __LINE__);                              \
                }                                                      \
        } while (0)

/** Tokenize argv and compare the token types, TOKENIZER_END is implied. */
static void _expect(int argc, char** argv, const enum TOKENIZER_TOKEN* types,
                    size_t count) {
        struct tokenizer_error error;
        struct tokenizer_tokens tokens = i_tokenize(argc, argv, &error);
        const char* name               = argc > 1 ? argv[1] : "(empty)";
        size_t i;

        CHECK(error.type == TOKENIZER_OK, name);
        CHECK(tokens.count == count + 1, name);
        if (tokens.count == count + 1) {
                for (i = 0; i < count; i++) {
                        CHECK(tokens.items[i].type == types[i], name);
                }
                CHECK(tokens.items[count].type == TOKENIZER_END, name);
        }
        i_tokenizer_free(&tokens);
}

#define EXPECT_ARGV(argv, ...)                                           \
        do {                                                             \
                const enum TOKENIZER_TOKEN types[] = {__VA_ARGS__};      \
                _expect((int)(sizeof(argv) / sizeof((argv)[0])), (argv), \
                        types, sizeof(types) / sizeof(types[0]));        \
        } while (0)

#define EXPECT_LINE(line, ...)                                  \
        do {                                                    \
                char* line_argv[] = {"oopdbms", (char*)(line)}; \
                EXPECT_ARGV(line_argv, __VA_ARGS__);            \
        } while (0)

/** Tokenize one line and expect a specific error at a specific offset. */
static void _expect_error(const char* line, enum tokenizer_error_type type,
                          size_t offset) {
        char* argv[] = {"oopdbms", (char*)line};
        struct tokenizer_error error;
        struct tokenizer_tokens tokens = i_tokenize(2, argv, &error);

        CHECK(error.type == type, line);
        CHECK(error.position.argv_index == 1, line);
        CHECK(error.position.offset == offset, line);
        CHECK(tokens.count == 0, line);
        i_tokenizer_free(&tokens);
}

static void _test_classes(void) {
        EXPECT_LINE(
            "CREATE CLASS Person IMPLEMENTS Named (name text, age integer)",
            TOKENIZER_CREATE_CLASS, TOKENIZER_IDENTIFIER, TOKENIZER_IMPLEMENTS,
            TOKENIZER_IDENTIFIER, TOKENIZER_LPAREN, TOKENIZER_IDENTIFIER,
            TOKENIZER_TYPE_TEXT, TOKENIZER_COMMA, TOKENIZER_IDENTIFIER,
            TOKENIZER_TYPE_INTEGER, TOKENIZER_RPAREN);
        EXPECT_LINE(
            "CREATE CLASS Employee EXTENDS Person (salary float, active bool)",
            TOKENIZER_CREATE_CLASS, TOKENIZER_IDENTIFIER, TOKENIZER_EXTENDS,
            TOKENIZER_IDENTIFIER, TOKENIZER_LPAREN, TOKENIZER_IDENTIFIER,
            TOKENIZER_TYPE_FLOAT, TOKENIZER_COMMA, TOKENIZER_IDENTIFIER,
            TOKENIZER_TYPE_BOOL, TOKENIZER_RPAREN);
        EXPECT_LINE("CREATE CLASS Book (title text, author pointer Person)",
                    TOKENIZER_CREATE_CLASS, TOKENIZER_IDENTIFIER,
                    TOKENIZER_LPAREN, TOKENIZER_IDENTIFIER, TOKENIZER_TYPE_TEXT,
                    TOKENIZER_COMMA, TOKENIZER_IDENTIFIER,
                    TOKENIZER_TYPE_POINTER, TOKENIZER_IDENTIFIER,
                    TOKENIZER_RPAREN);
        EXPECT_LINE("CREATE INTERFACE Named (name text)",
                    TOKENIZER_CREATE_INTERFACE, TOKENIZER_IDENTIFIER,
                    TOKENIZER_LPAREN, TOKENIZER_IDENTIFIER, TOKENIZER_TYPE_TEXT,
                    TOKENIZER_RPAREN);
        EXPECT_LINE("DROP CLASS Novel", TOKENIZER_DROP_CLASS,
                    TOKENIZER_IDENTIFIER);
        EXPECT_LINE("DROP INTERFACE Named", TOKENIZER_DROP_INTERFACE,
                    TOKENIZER_IDENTIFIER);
}

static void _test_alter(void) {
        EXPECT_LINE("ALTER CLASS Book ADD FIELD pages integer",
                    TOKENIZER_ALTER_CLASS, TOKENIZER_IDENTIFIER,
                    TOKENIZER_ADD_FIELD, TOKENIZER_IDENTIFIER,
                    TOKENIZER_TYPE_INTEGER);
        EXPECT_LINE("ALTER CLASS Book DROP FIELD page_count",
                    TOKENIZER_ALTER_CLASS, TOKENIZER_IDENTIFIER,
                    TOKENIZER_DROP_FIELD, TOKENIZER_IDENTIFIER);
        EXPECT_LINE("ALTER CLASS Book RENAME FIELD pages page_count",
                    TOKENIZER_ALTER_CLASS, TOKENIZER_IDENTIFIER,
                    TOKENIZER_RENAME_FIELD, TOKENIZER_IDENTIFIER,
                    TOKENIZER_IDENTIFIER);
        EXPECT_LINE("ALTER CLASS Book RENAME Novel", TOKENIZER_ALTER_CLASS,
                    TOKENIZER_IDENTIFIER, TOKENIZER_RENAME,
                    TOKENIZER_IDENTIFIER);
        EXPECT_LINE("ALTER CLASS Employee IMPLEMENTS Named",
                    TOKENIZER_ALTER_CLASS, TOKENIZER_IDENTIFIER,
                    TOKENIZER_IMPLEMENTS, TOKENIZER_IDENTIFIER);
}

static void _test_functions(void) {
        EXPECT_LINE(
            "CREATE FUNCTION Employee.raised (percent float) RETURNS float AS "
            "salary * (1 + percent / 100)",
            TOKENIZER_CREATE_FUNCTION, TOKENIZER_IDENTIFIER, TOKENIZER_DOT,
            TOKENIZER_IDENTIFIER, TOKENIZER_LPAREN, TOKENIZER_IDENTIFIER,
            TOKENIZER_TYPE_FLOAT, TOKENIZER_RPAREN, TOKENIZER_RETURNS,
            TOKENIZER_TYPE_FLOAT, TOKENIZER_AS, TOKENIZER_IDENTIFIER,
            TOKENIZER_STAR, TOKENIZER_LPAREN, TOKENIZER_LITERAL_INTEGER,
            TOKENIZER_PLUS, TOKENIZER_IDENTIFIER, TOKENIZER_SLASH,
            TOKENIZER_LITERAL_INTEGER, TOKENIZER_RPAREN);
        EXPECT_LINE("CALL Employee.raised (10) ON 7", TOKENIZER_CALL,
                    TOKENIZER_IDENTIFIER, TOKENIZER_DOT, TOKENIZER_IDENTIFIER,
                    TOKENIZER_LPAREN, TOKENIZER_LITERAL_INTEGER,
                    TOKENIZER_RPAREN, TOKENIZER_ON, TOKENIZER_LITERAL_INTEGER);
        EXPECT_LINE("DROP FUNCTION Employee.raised", TOKENIZER_DROP_FUNCTION,
                    TOKENIZER_IDENTIFIER, TOKENIZER_DOT, TOKENIZER_IDENTIFIER);
}

static void _test_objects(void) {
        EXPECT_LINE("NEW Person (name = \"Frank Herbert\", age = 65)",
                    TOKENIZER_NEW, TOKENIZER_IDENTIFIER, TOKENIZER_LPAREN,
                    TOKENIZER_IDENTIFIER, TOKENIZER_EQUAL,
                    TOKENIZER_LITERAL_TEXT, TOKENIZER_COMMA,
                    TOKENIZER_IDENTIFIER, TOKENIZER_EQUAL,
                    TOKENIZER_LITERAL_INTEGER, TOKENIZER_RPAREN);
        EXPECT_LINE(
            "NEW Book (title = \"Dune\", price = 9.99, author = Person.1)",
            TOKENIZER_NEW, TOKENIZER_IDENTIFIER, TOKENIZER_LPAREN,
            TOKENIZER_IDENTIFIER, TOKENIZER_EQUAL, TOKENIZER_LITERAL_TEXT,
            TOKENIZER_COMMA, TOKENIZER_IDENTIFIER, TOKENIZER_EQUAL,
            TOKENIZER_LITERAL_FLOAT, TOKENIZER_COMMA, TOKENIZER_IDENTIFIER,
            TOKENIZER_EQUAL, TOKENIZER_IDENTIFIER, TOKENIZER_DOT,
            TOKENIZER_LITERAL_INTEGER, TOKENIZER_RPAREN);
        EXPECT_LINE("FIND Book WHERE price < 20", TOKENIZER_FIND,
                    TOKENIZER_IDENTIFIER, TOKENIZER_WHERE, TOKENIZER_IDENTIFIER,
                    TOKENIZER_LESS, TOKENIZER_LITERAL_INTEGER);
        EXPECT_LINE("FIND Employee WHERE active = true", TOKENIZER_FIND,
                    TOKENIZER_IDENTIFIER, TOKENIZER_WHERE, TOKENIZER_IDENTIFIER,
                    TOKENIZER_EQUAL, TOKENIZER_LITERAL_TRUE);
        EXPECT_LINE("SET Book 1 (price = 12.50)", TOKENIZER_SET,
                    TOKENIZER_IDENTIFIER, TOKENIZER_LITERAL_INTEGER,
                    TOKENIZER_LPAREN, TOKENIZER_IDENTIFIER, TOKENIZER_EQUAL,
                    TOKENIZER_LITERAL_FLOAT, TOKENIZER_RPAREN);
        EXPECT_LINE("DELETE Book 1", TOKENIZER_DELETE, TOKENIZER_IDENTIFIER,
                    TOKENIZER_LITERAL_INTEGER);
}

static void _test_meta(void) {
        EXPECT_LINE("LIST CLASSES", TOKENIZER_LIST_CLASSES);
        EXPECT_LINE("LIST INTERFACES", TOKENIZER_LIST_INTERFACES);
        EXPECT_LINE("DESCRIBE Employee", TOKENIZER_DESCRIBE,
                    TOKENIZER_IDENTIFIER);
}

static void _test_splitting(void) {
        char* split[] = {"oopdbms", "CREATE", "CLASS", "Person",
                         "(name text, age integer)"};
        char* empty[] = {"oopdbms"};
        struct tokenizer_error error;
        struct tokenizer_tokens none = i_tokenize(1, empty, &error);

        EXPECT_ARGV(split, TOKENIZER_CREATE_CLASS, TOKENIZER_IDENTIFIER,
                    TOKENIZER_LPAREN, TOKENIZER_IDENTIFIER, TOKENIZER_TYPE_TEXT,
                    TOKENIZER_COMMA, TOKENIZER_IDENTIFIER,
                    TOKENIZER_TYPE_INTEGER, TOKENIZER_RPAREN);
        CHECK(error.type == TOKENIZER_OK, "empty: ok");
        CHECK(none.count == 1, "empty: count");
        CHECK(none.count == 1 && none.items[0].type == TOKENIZER_END,
              "empty: only END");
        i_tokenizer_free(&none);
}

static void _test_no_spaces(void) {
        EXPECT_LINE("NEW Book(price=9.99,pages=300)", TOKENIZER_NEW,
                    TOKENIZER_IDENTIFIER, TOKENIZER_LPAREN,
                    TOKENIZER_IDENTIFIER, TOKENIZER_EQUAL,
                    TOKENIZER_LITERAL_FLOAT, TOKENIZER_COMMA,
                    TOKENIZER_IDENTIFIER, TOKENIZER_EQUAL,
                    TOKENIZER_LITERAL_INTEGER, TOKENIZER_RPAREN);
        EXPECT_LINE("x<=1>2>=3!=4-5", TOKENIZER_IDENTIFIER,
                    TOKENIZER_LESS_EQUAL, TOKENIZER_LITERAL_INTEGER,
                    TOKENIZER_GREATER, TOKENIZER_LITERAL_INTEGER,
                    TOKENIZER_GREATER_EQUAL, TOKENIZER_LITERAL_INTEGER,
                    TOKENIZER_NOT_EQUAL, TOKENIZER_LITERAL_INTEGER,
                    TOKENIZER_MINUS, TOKENIZER_LITERAL_INTEGER);
}

static void _test_quotes(void) {
        EXPECT_LINE("NEW Book (title = 'Dune')", TOKENIZER_NEW,
                    TOKENIZER_IDENTIFIER, TOKENIZER_LPAREN,
                    TOKENIZER_IDENTIFIER, TOKENIZER_EQUAL,
                    TOKENIZER_LITERAL_TEXT, TOKENIZER_RPAREN);
        EXPECT_LINE("SET P 1 (n = \"a\\\"b)\")", TOKENIZER_SET,
                    TOKENIZER_IDENTIFIER, TOKENIZER_LITERAL_INTEGER,
                    TOKENIZER_LPAREN, TOKENIZER_IDENTIFIER, TOKENIZER_EQUAL,
                    TOKENIZER_LITERAL_TEXT, TOKENIZER_RPAREN);
}

static void _test_lexemes(void) {
        char* argv[] = {"oopdbms", "CREATE", "CLASS", "Person"};
        struct tokenizer_error error;
        struct tokenizer_tokens tokens = i_tokenize(4, argv, &error);

        CHECK(error.type == TOKENIZER_OK, "lexemes: ok");
        CHECK(tokens.count == 3, "lexemes: count");
        if (tokens.count == 3) {
                CHECK(tokens.items[0].type == TOKENIZER_CREATE_CLASS,
                      "lexemes: phrase type");
                CHECK(tokens.items[0].lexeme == argv[1],
                      "lexemes: phrase points at first word");
                CHECK(tokens.items[0].length == 6, "lexemes: phrase length");
                CHECK(tokens.items[0].position.argv_index == 1,
                      "lexemes: phrase argv_index");
                CHECK(tokens.items[1].lexeme == argv[3],
                      "lexemes: identifier lexeme");
                CHECK(tokens.items[1].position.argv_index == 3,
                      "lexemes: identifier argv_index");
                CHECK(tokens.items[2].position.argv_index == 4,
                      "lexemes: end position");
        }
        i_tokenizer_free(&tokens);
}

static void _test_text(void) {
        char* argv[] = {
            "oopdbms",
            "NEW Person (name = \"Frank \\\"F\\\" Herbert\", age = 65, "
            "price = 9.99, note = 'Dune', empty = \"\")"};
        struct tokenizer_error error;
        struct tokenizer_tokens tokens = i_tokenize(2, argv, &error);

        CHECK(error.type == TOKENIZER_OK, "text: ok");
        CHECK(tokens.count == 24, "text: count");
        if (tokens.count == 24) {
                CHECK(tokens.items[0].text == NULL, "text: keyword has none");
                CHECK(strcmp(tokens.items[1].text, "Person") == 0,
                      "text: identifier name");
                CHECK(tokens.items[2].text == NULL, "text: paren has none");
                CHECK(strcmp(tokens.items[3].text, "name") == 0,
                      "text: field name");
                CHECK(strcmp(tokens.items[5].text, "Frank \"F\" Herbert") == 0,
                      "text: unquoted and unescaped");
                CHECK(strcmp(tokens.items[9].text, "65") == 0,
                      "text: integer digits");
                CHECK(strcmp(tokens.items[13].text, "9.99") == 0,
                      "text: float digits");
                CHECK(strcmp(tokens.items[17].text, "Dune") == 0,
                      "text: single quotes");
                CHECK(strcmp(tokens.items[21].text, "") == 0,
                      "text: empty text");
                CHECK(tokens.items[23].text == NULL, "text: END has none");
        }
        i_tokenizer_free(&tokens);
}

static void _test_errors(void) {
        _expect_error("NEW Person (name = \"Frank)",
                      TOKENIZER_ERROR_UNTERMINATED_TEXT, 19);
        _expect_error("CREATE CLASS @", TOKENIZER_ERROR_ILLEGAL_CHARACTER, 13);
        _expect_error("x ! y", TOKENIZER_ERROR_ILLEGAL_CHARACTER, 2);
        _expect_error("CREATE Person", TOKENIZER_ERROR_INCOMPLETE_PHRASE, 0);
        _expect_error("DROP", TOKENIZER_ERROR_INCOMPLETE_PHRASE, 0);
        _expect_error("FIND Book WHERE price < 12abc",
                      TOKENIZER_ERROR_BAD_NUMBER, 24);
}

static void _test_names(void) {
        int i;

        for (i = 0; i < TOKENIZER_COUNT; i++) {
                CHECK(strcmp(i_tokenizer_token_name((enum TOKENIZER_TOKEN)i),
                             "unknown token") != 0,
                      "every token has a name");
        }
        CHECK(strcmp(i_tokenizer_token_name(TOKENIZER_COUNT),
                     "unknown token") == 0,
              "out of range has no name");
}

int main(void) {
        _test_classes();
        _test_alter();
        _test_functions();
        _test_objects();
        _test_meta();
        _test_splitting();
        _test_no_spaces();
        _test_quotes();
        _test_lexemes();
        _test_text();
        _test_errors();
        _test_names();
        printf("tokenizer tests:\n");
        printf("%d checks, %d failures\n", checks, failures);
        return failures != 0;
}
