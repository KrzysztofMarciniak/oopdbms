/**
 * @file main.c
 * @brief Analyzer unit tests, run with `make test`.
 */

#include "interface/analyzer/main.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interface/tokenizer/main.h"

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

/** Equal strings, NULL only equals NULL. */
static int _same(const char* a, const char* b) {
        if (a == NULL || b == NULL) {
                return a == b;
        }
        return strcmp(a, b) == 0;
}

/** Tokenize and analyze one line, the caller frees the statement. */
static struct analyzer_statement _analyze(const char* line,
                                          struct analyzer_error* error) {
        char* argv[] = {"oopdbms", (char*)line};
        struct tokenizer_error token_error;
        struct tokenizer_tokens tokens = i_tokenize(2, argv, &token_error);
        struct analyzer_statement statement;

        CHECK(token_error.type == TOKENIZER_OK, line);
        statement = i_analyze(&tokens, error);
        i_tokenizer_free(&tokens);
        return statement;
}

/** Analyze a line that must be valid. */
static struct analyzer_statement _ok(const char* line,
                                     enum ANALYZER_STATEMENT kind) {
        struct analyzer_error error;
        struct analyzer_statement statement = _analyze(line, &error);

        CHECK(error.type == ANALYZER_OK, line);
        CHECK(statement.kind == kind, line);
        return statement;
}

/** Analyze a line that must fail the way the arguments say. */
static void _fails(const char* line, enum TOKENIZER_TOKEN expected,
                   enum TOKENIZER_TOKEN found, size_t offset) {
        struct analyzer_error error;
        struct analyzer_statement statement = _analyze(line, &error);

        CHECK(error.type == ANALYZER_ERROR_UNEXPECTED_TOKEN, line);
        CHECK(error.expected == expected, line);
        CHECK(error.found == found, line);
        if (found != TOKENIZER_END) {
                CHECK(error.position.argv_index == 1, line);
                CHECK(error.position.offset == offset, line);
        }
        CHECK(statement.kind == ANALYZER_INVALID, line);
        i_analyzer_free(&statement);
}

/** Check the additional context retained for an analyzer diagnostic. */
static void _diagnostic(const char* line,
                        enum analyzer_expected_kind expected_kind,
                        const char* found_text) {
        struct analyzer_error error;
        struct analyzer_statement statement = _analyze(line, &error);

        CHECK(error.type == ANALYZER_ERROR_UNEXPECTED_TOKEN, line);
        CHECK(error.expected_kind == expected_kind, line);
        CHECK(_same(error.found_text, found_text), line);
        CHECK(statement.kind == ANALYZER_INVALID, line);
        i_analyzer_free(&statement);
}

static void _test_diagnostic_truncation(void) {
        char line[ANALYZER_ERROR_TOKEN_TEXT_MAX + 20];
        size_t length = sizeof line - 1;
        struct analyzer_error error;
        struct analyzer_statement statement;

        memset(line, 'x', length);
        line[length] = '\0';
        statement    = _analyze(line, &error);
        CHECK(error.type == ANALYZER_ERROR_UNEXPECTED_TOKEN,
              "diagnostic text truncation");
        CHECK(error.found_text_length == length, "diagnostic original length");
        CHECK(strlen(error.found_text) == ANALYZER_ERROR_TOKEN_TEXT_MAX - 1,
              "diagnostic copied length");
        CHECK(statement.kind == ANALYZER_INVALID,
              "diagnostic truncated statement");
        i_analyzer_free(&statement);
}

static void _test_classes(void) {
        struct analyzer_statement st =
            _ok("CREATE CLASS Employee EXTENDS Person IMPLEMENTS Named, Tagged "
                "(salary float, active bool)",
                ANALYZER_CREATE_CLASS);

        CHECK(_same(st.name, "Employee"), "class name");
        CHECK(_same(st.parent, "Person"), "class parent");
        CHECK(st.implements.count == 2, "implements count");
        CHECK(
            st.implements.count == 2 && _same(st.implements.items[0], "Named"),
            "implements first");
        CHECK(
            st.implements.count == 2 && _same(st.implements.items[1], "Tagged"),
            "implements second");
        CHECK(st.fields.count == 2, "field count");
        CHECK(st.fields.count == 2 &&
                  _same(st.fields.items[0].name, "salary") &&
                  st.fields.items[0].type.kind == ANALYZER_TYPE_FLOAT,
              "field salary");
        CHECK(st.fields.count == 2 &&
                  _same(st.fields.items[1].name, "active") &&
                  st.fields.items[1].type.kind == ANALYZER_TYPE_BOOL,
              "field active");
        i_analyzer_free(&st);

        st =
            _ok("CREATE CLASS Book (title text, price float, author pointer "
                "Person)",
                ANALYZER_CREATE_CLASS);
        CHECK(st.parent == NULL && st.implements.count == 0, "plain class");
        CHECK(st.fields.count == 3 &&
                  st.fields.items[2].type.kind == ANALYZER_TYPE_POINTER &&
                  _same(st.fields.items[2].type.class_name, "Person"),
              "pointer field");
        i_analyzer_free(&st);

        st = _ok("CREATE INTERFACE Named (name text)",
                 ANALYZER_CREATE_INTERFACE);
        CHECK(_same(st.name, "Named") && st.fields.count == 1, "interface");
        i_analyzer_free(&st);

        st = _ok("DROP CLASS Novel", ANALYZER_DROP_CLASS);
        CHECK(_same(st.name, "Novel"), "drop class");
        i_analyzer_free(&st);

        st = _ok("DROP INTERFACE Named", ANALYZER_DROP_INTERFACE);
        CHECK(_same(st.name, "Named"), "drop interface");
        i_analyzer_free(&st);
}

static void _test_alter(void) {
        struct analyzer_statement st =
            _ok("ALTER CLASS Book ADD FIELD pages integer",
                ANALYZER_ALTER_CLASS_ADD_FIELD);

        CHECK(_same(st.name, "Book") && st.fields.count == 1 &&
                  _same(st.fields.items[0].name, "pages") &&
                  st.fields.items[0].type.kind == ANALYZER_TYPE_INTEGER,
              "add field");
        i_analyzer_free(&st);

        st = _ok("ALTER CLASS Book DROP FIELD page_count",
                 ANALYZER_ALTER_CLASS_DROP_FIELD);
        CHECK(_same(st.name, "Book") && _same(st.field, "page_count"),
              "drop field");
        i_analyzer_free(&st);

        st = _ok("ALTER CLASS Book RENAME FIELD pages page_count",
                 ANALYZER_ALTER_CLASS_RENAME_FIELD);
        CHECK(_same(st.field, "pages") && _same(st.new_name, "page_count"),
              "rename field");
        i_analyzer_free(&st);

        st = _ok("ALTER CLASS Book RENAME Novel", ANALYZER_ALTER_CLASS_RENAME);
        CHECK(_same(st.name, "Book") && _same(st.new_name, "Novel") &&
                  st.field == NULL,
              "rename class");
        i_analyzer_free(&st);

        st = _ok("ALTER CLASS Employee EXTENDS Person",
                 ANALYZER_ALTER_CLASS_EXTENDS);
        CHECK(_same(st.parent, "Person"), "alter extends");
        i_analyzer_free(&st);

        st = _ok("ALTER CLASS Employee IMPLEMENTS Named",
                 ANALYZER_ALTER_CLASS_IMPLEMENTS);
        CHECK(
            st.implements.count == 1 && _same(st.implements.items[0], "Named"),
            "alter implements");
        i_analyzer_free(&st);
}

static void _test_functions(void) {
        struct analyzer_statement st = _ok(
            "CREATE FUNCTION Employee.raised (percent float) RETURNS float AS "
            "salary * (1 + percent / 100)",
            ANALYZER_CREATE_FUNCTION);
        struct analyzer_expression* e = st.expression;

        CHECK(_same(st.name, "Employee") && _same(st.function, "raised"),
              "function names");
        CHECK(st.fields.count == 1 && _same(st.fields.items[0].name, "percent"),
              "function argument");
        CHECK(st.return_type.kind == ANALYZER_TYPE_FLOAT, "return type");
        CHECK(e != NULL && e->kind == ANALYZER_EXPRESSION_BINARY &&
                  e->op == ANALYZER_OPERATOR_MULTIPLY,
              "root is multiply");
        CHECK(e != NULL && e->left->kind == ANALYZER_EXPRESSION_FIELD &&
                  _same(e->left->field, "salary"),
              "left is salary");
        CHECK(e != NULL && e->right->kind == ANALYZER_EXPRESSION_BINARY &&
                  e->right->op == ANALYZER_OPERATOR_ADD,
              "right is the sum");
        CHECK(e != NULL &&
                  e->right->left->kind == ANALYZER_EXPRESSION_LITERAL &&
                  _same(e->right->left->value.text, "1"),
              "sum starts with 1");
        CHECK(e != NULL &&
                  e->right->right->kind == ANALYZER_EXPRESSION_BINARY &&
                  e->right->right->op == ANALYZER_OPERATOR_DIVIDE,
              "sum ends with a division");
        i_analyzer_free(&st);

        st = _ok("CALL Employee.raised (10, -2.5) ON 7", ANALYZER_CALL);
        CHECK(_same(st.name, "Employee") && _same(st.function, "raised") &&
                  _same(st.id, "7"),
              "call names");
        CHECK(st.values.count == 2 &&
                  st.values.items[0].kind == ANALYZER_VALUE_INTEGER &&
                  _same(st.values.items[0].text, "10") &&
                  st.values.items[1].kind == ANALYZER_VALUE_FLOAT &&
                  _same(st.values.items[1].text, "-2.5"),
              "call values");
        i_analyzer_free(&st);

        st = _ok("DROP FUNCTION Employee.raised", ANALYZER_DROP_FUNCTION);
        CHECK(_same(st.name, "Employee") && _same(st.function, "raised"),
              "drop function");
        i_analyzer_free(&st);
}

static void _test_objects(void) {
        struct analyzer_statement st =
            _ok("NEW Book (title = \"Dune\", price = 9.99, author = Person.1, "
                "stock = -5, new = true)",
                ANALYZER_NEW);

        CHECK(_same(st.name, "Book") && st.assignments.count == 5, "new");
        if (st.assignments.count == 5) {
                struct analyzer_assignment* a = st.assignments.items;

                CHECK(_same(a[0].field, "title") &&
                          a[0].value.kind == ANALYZER_VALUE_TEXT &&
                          _same(a[0].value.text, "Dune"),
                      "text value");
                CHECK(a[1].value.kind == ANALYZER_VALUE_FLOAT &&
                          _same(a[1].value.text, "9.99"),
                      "float value");
                CHECK(a[2].value.kind == ANALYZER_VALUE_POINTER &&
                          _same(a[2].value.class_name, "Person") &&
                          _same(a[2].value.text, "1"),
                      "pointer value");
                CHECK(a[3].value.kind == ANALYZER_VALUE_INTEGER &&
                          _same(a[3].value.text, "-5"),
                      "negative value");
                CHECK(a[4].value.kind == ANALYZER_VALUE_BOOL &&
                          _same(a[4].value.text, "true"),
                      "bool value");
        }
        i_analyzer_free(&st);

        st = _ok("FIND Person", ANALYZER_FIND);
        CHECK(_same(st.name, "Person") && st.expression == NULL, "find all");
        i_analyzer_free(&st);

        st = _ok("FIND Book WHERE price < 20", ANALYZER_FIND);
        CHECK(st.expression != NULL &&
                  st.expression->kind == ANALYZER_EXPRESSION_BINARY &&
                  st.expression->op == ANALYZER_OPERATOR_LESS &&
                  _same(st.expression->left->field, "price") &&
                  _same(st.expression->right->value.text, "20"),
              "find where less");
        i_analyzer_free(&st);

        st = _ok("FIND Employee WHERE active = true", ANALYZER_FIND);
        CHECK(st.expression != NULL &&
                  st.expression->op == ANALYZER_OPERATOR_EQUAL &&
                  st.expression->right->value.kind == ANALYZER_VALUE_BOOL,
              "find where bool");
        i_analyzer_free(&st);

        st = _ok("SET Book 1 (price = 12.50)", ANALYZER_SET);
        CHECK(_same(st.name, "Book") && _same(st.id, "1") &&
                  st.assignments.count == 1,
              "set");
        i_analyzer_free(&st);

        st = _ok("DELETE Book 1", ANALYZER_DELETE);
        CHECK(_same(st.name, "Book") && _same(st.id, "1"), "delete");
        i_analyzer_free(&st);
}

static void _test_meta(void) {
        struct analyzer_statement st =
            _ok("LIST CLASSES", ANALYZER_LIST_CLASSES);

        i_analyzer_free(&st);
        st = _ok("LIST INTERFACES", ANALYZER_LIST_INTERFACES);
        i_analyzer_free(&st);
        st = _ok("DESCRIBE Employee", ANALYZER_DESCRIBE);
        CHECK(_same(st.name, "Employee"), "describe");
        i_analyzer_free(&st);
}

static void _test_precedence(void) {
        struct analyzer_statement st =
            _ok("CREATE FUNCTION A.f (x integer) RETURNS integer AS 1 + 2 * 3",
                ANALYZER_CREATE_FUNCTION);

        CHECK(st.expression->op == ANALYZER_OPERATOR_ADD &&
                  st.expression->right->op == ANALYZER_OPERATOR_MULTIPLY,
              "multiply binds tighter");
        i_analyzer_free(&st);

        st =
            _ok("CREATE FUNCTION A.f (x integer) RETURNS integer AS 10 - 4 - 3",
                ANALYZER_CREATE_FUNCTION);
        CHECK(st.expression->op == ANALYZER_OPERATOR_SUBTRACT &&
                  st.expression->left->kind == ANALYZER_EXPRESSION_BINARY &&
                  _same(st.expression->right->value.text, "3"),
              "left associative");
        i_analyzer_free(&st);

        st = _ok("CREATE FUNCTION A.f (x integer) RETURNS integer AS -x",
                 ANALYZER_CREATE_FUNCTION);
        CHECK(st.expression->kind == ANALYZER_EXPRESSION_NEGATE &&
                  _same(st.expression->left->field, "x"),
              "negate");
        i_analyzer_free(&st);

        st = _ok("CREATE FUNCTION A.f (x integer) RETURNS bool AS x >= 5",
                 ANALYZER_CREATE_FUNCTION);
        CHECK(st.expression->op == ANALYZER_OPERATOR_GREATER_EQUAL,
              "comparison in a function body");
        i_analyzer_free(&st);
}

static void _test_errors(void) {
        struct analyzer_error error;
        struct analyzer_statement st;
        char* empty[] = {"oopdbms"};
        struct tokenizer_error token_error;
        struct tokenizer_tokens none = i_tokenize(1, empty, &token_error);

        _fails("CREATE CLASS Person", TOKENIZER_LPAREN, TOKENIZER_END, 0);
        _fails("CREATE CLASS Person ()", TOKENIZER_IDENTIFIER, TOKENIZER_RPAREN,
               21);
        _fails("DROP CLASS Person extra", TOKENIZER_END, TOKENIZER_IDENTIFIER,
               18);
        _fails("Person", TOKENIZER_ILLEGAL, TOKENIZER_IDENTIFIER, 0);
        _fails("FIND Book WHERE price", TOKENIZER_ILLEGAL, TOKENIZER_END, 0);
        _fails("ALTER CLASS Book", TOKENIZER_ILLEGAL, TOKENIZER_END, 0);
        _fails("NEW Book (price 5)", TOKENIZER_EQUAL, TOKENIZER_LITERAL_INTEGER,
               16);
        _fails("CALL A.b (1) ON x", TOKENIZER_LITERAL_INTEGER,
               TOKENIZER_IDENTIFIER, 16);
        _fails("NEW Book (price = -x)", TOKENIZER_ILLEGAL, TOKENIZER_IDENTIFIER,
               19);
        _fails("CREATE FUNCTION A.f (x integer) RETURNS integer AS (1 + 2",
               TOKENIZER_RPAREN, TOKENIZER_END, 0);
        _diagnostic("Person", ANALYZER_EXPECT_STATEMENT, "Person");
        _diagnostic("CREATE CLASS Book (price unknown)", ANALYZER_EXPECT_TYPE,
                    "unknown");
        _diagnostic("NEW Book (price = -x)", ANALYZER_EXPECT_NUMBER, "x");
        _diagnostic("NEW Book (price = )", ANALYZER_EXPECT_LITERAL, ")");
        _diagnostic("CREATE FUNCTION A.f (x integer) RETURNS integer AS )",
                    ANALYZER_EXPECT_EXPRESSION, ")");
        _diagnostic("FIND Book WHERE price", ANALYZER_EXPECT_COMPARISON, "");
        _diagnostic("ALTER CLASS Book invalid", ANALYZER_EXPECT_ALTER_ACTION,
                    "invalid");
        _test_diagnostic_truncation();

        st = i_analyze(&none, &error);
        CHECK(error.type == ANALYZER_ERROR_EMPTY, "empty input");
        CHECK(st.kind == ANALYZER_INVALID, "empty statement");
        i_tokenizer_free(&none);
}

/** Nesting and long chains must neither crash nor overflow the stack. */
static void _test_limits(void) {
        const char* head =
            "CREATE FUNCTION A.f (x integer) RETURNS integer AS ";
        size_t head_length = strlen(head);
        size_t depth       = 400;
        size_t terms       = 20000;
        char* line         = malloc(head_length + 2 * depth + 4 * terms + 8);
        struct analyzer_error error;
        struct analyzer_statement st;
        size_t i;
        size_t n;

        if (line == NULL) {
                CHECK(0, "limits: allocation");
                return;
        }
        n = head_length;
        memcpy(line, head, head_length);
        for (i = 0; i < depth; i++) {
                line[n++] = '(';
        }
        line[n++] = '1';
        for (i = 0; i < depth; i++) {
                line[n++] = ')';
        }
        line[n] = '\0';
        st      = _analyze(line, &error);
        CHECK(error.type == ANALYZER_ERROR_TOO_DEEP, "limits: nesting");
        CHECK(st.kind == ANALYZER_INVALID, "limits: nesting statement");

        n         = head_length;
        line[n++] = '1';
        for (i = 0; i < terms; i++) {
                line[n++] = '+';
                line[n++] = '1';
        }
        line[n] = '\0';
        st      = _analyze(line, &error);
        CHECK(error.type == ANALYZER_OK, "limits: long chain");
        CHECK(st.kind == ANALYZER_CREATE_FUNCTION, "limits: chain statement");
        i_analyzer_free(&st);
        free(line);
}

int main(void) {
        _test_classes();
        _test_alter();
        _test_functions();
        _test_objects();
        _test_meta();
        _test_precedence();
        _test_errors();
        _test_limits();
        printf("analyzer tests:\n");
        printf("%d checks, %d failures\n", checks, failures);
        return failures != 0;
}
