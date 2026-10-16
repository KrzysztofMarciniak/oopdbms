# oopdbms
object oriented programming focused data base managment system

## Commands

### Classes
```
CREATE CLASS <name> [EXTENDS <class>] [IMPLEMENTS <interface>[, ...]] (<field> <type>[, ...])
DROP CLASS <name>
ALTER CLASS <name> ADD FIELD <field> <type>
ALTER CLASS <name> DROP FIELD <field>
ALTER CLASS <name> RENAME FIELD <old> <new>
ALTER CLASS <name> RENAME <new>
ALTER CLASS <name> EXTENDS <class>
ALTER CLASS <name> IMPLEMENTS <interface>
```

### Interfaces
```
CREATE INTERFACE <name> (<field> <type>[, ...])
DROP INTERFACE <name>
```

### Functions
```
CREATE FUNCTION <class>.<name> (<arg> <type>[, ...]) RETURNS <type> AS <expression>
DROP FUNCTION <class>.<name>
CALL <class>.<name> (<value>[, ...]) ON <id>
```

### Objects
```
NEW <class> (<field> = <value>[, ...])
FIND <class> [WHERE <condition>]
SET <class> <id> (<field> = <value>[, ...])
DELETE <class> <id>
```

### Meta
```
LIST CLASSES
LIST INTERFACES
DESCRIBE <class>
```

### Types
```
integer
float
text
bool
pointer <class>    relation to <class> (like a SQL foreign key); a value is <class>.<id>
```

## Examples

### Classes and interfaces
```
CREATE INTERFACE Named (name text)
CREATE CLASS Person IMPLEMENTS Named (name text, age integer)
CREATE CLASS Employee EXTENDS Person (salary float, active bool)
CREATE CLASS Book (title text, price float, author pointer Person)

ALTER CLASS Book ADD FIELD pages integer
ALTER CLASS Book RENAME FIELD pages page_count
ALTER CLASS Book DROP FIELD page_count
ALTER CLASS Book RENAME Novel
ALTER CLASS Employee IMPLEMENTS Named

DROP CLASS Novel
DROP INTERFACE Named
```

### Functions
```
CREATE FUNCTION Employee.raised (percent float) RETURNS float AS salary * (1 + percent / 100)
CALL Employee.raised (10) ON 7
DROP FUNCTION Employee.raised
```

### Objects
```
NEW Person (name = "Frank Herbert", age = 65)
NEW Book (title = "Dune", price = 9.99, author = Person.1)

FIND Person
FIND Book WHERE price < 20
FIND Employee WHERE active = true

SET Book 1 (price = 12.50)
DELETE Book 1
```

### Meta
```
LIST CLASSES
LIST INTERFACES
DESCRIBE Employee
```

## Building

```
make          format, then build to build/oopdbms
make run      build and run
make test     build and run every test in src/tests/ (run from the project root)
make docs     doxygen html to build/docs/html/index.html
make format   clang-format all sources
make clean    remove build/
```

Only plain C99 and Make are needed. `make docs` needs doxygen.

## How a command runs

The whole command line is one statement. Quote it as one argument or pass it
as several, both work:

```
oopdbms 'CREATE CLASS Person (name text, age integer)'
oopdbms CREATE CLASS Person '(name text, age integer)'
```

A statement goes through these steps, a failing step prints its error to
stderr and exits with 1:

```
1. tokenizer       argv -> tokens
2. analyzer        tokens -> checked statement (syntax only)
3. file check      is the database file usable for this statement
4. execute         (not written yet)
```

Every step is an interface under `src/interface/<step>/`, the code lives in
`src/implementation/<step>/` and the errors in `src/error/<step>/`.

### Tokenizer

Splits argv (argv[0] is skipped) like a small shell, then classifies the
pieces.

```
words        [A-Za-z_][A-Za-z0-9_]*, keywords are case-sensitive:
             commands UPPERCASE, types and true/false lowercase
numbers      42   9.99      (a sign is a separate - token)
text         "Frank Herbert"   or   'Dune'   with \" and \\ escapes
operators    =  !=  <  <=  >  >=  +  -  *  /
punctuation  (  )  ,  .      split words even without spaces: Person.1
```

Two-word commands are one token, even when they are separate arguments:
`CREATE CLASS`, `DROP CLASS`, `ALTER CLASS`, `CREATE INTERFACE`,
`DROP INTERFACE`, `CREATE FUNCTION`, `DROP FUNCTION`, `LIST CLASSES`,
`LIST INTERFACES`, `ADD FIELD`, `DROP FIELD`, `RENAME FIELD`. `ALTER CLASS`
has the class name in the middle, so its action (`ADD FIELD`, `RENAME`,
`EXTENDS`, ...) is its own token after the name.

Every token carries its type (`enum TOKENIZER_TOKEN`), its position (argument
and offset) and, for names and literals, an owned copy of what was written:
the name, the text without quotes and escapes, or the digits. Numbers are not
converted.

Errors: illegal character (`@`, a lone `!`), unterminated text, malformed
number (`12abc`), incomplete command (`CREATE Person`).

### Analyzer

Checks the grammar from the Commands section and builds one
`struct analyzer_statement` that owns all its strings. It does not look at the
database: whether a class exists or a type fits is not its job.

```
expression  additive [comparison additive]
additive    term {(+ | -) term}
term        unary {(* | /) unary}
unary       - unary | primary
primary     <name> | <literal> | ( additive )
condition   additive comparison additive        (WHERE needs a comparison)
value       <literal> | -<number> | <class>.<id>
```

`*` and `/` bind tighter than `+` and `-`, chains are left-associative.
Field lists, argument lists, CALL values and assignments need at least one
entry. Parentheses and minus signs may nest 128 deep.

Errors say what was expected and what was found:

```
analyzer: expected ')' but found end of input
analyzer: unexpected a name at argument 1, offset 0
```

### File check

Decides whether the database file can run the statement, and creates it when
it has to.

```
FIND, LIST, DESCRIBE, CALL   read only: the data must exist already, a
                             missing file is an error and nothing is created
everything else              writes: a missing file is created with a valid
                             header, an existing one must be writable
```

The file is `database.oopdb` in the current directory, or the path in the
`OOPDB` environment variable:

```
OOPDB=/srv/books.oopdb oopdbms FIND Book
```

The header is checked on every run. All integers are little-endian:

```
bytes 0..7    magic "OOPDB" followed by three zero bytes
bytes 8..11   format version, u32, currently 1
```

Errors: not found, cannot open/read/create (with the OS reason), file too
short, wrong magic (not an oopdb file), unsupported version.

## Source layout

```
src/main.c                        runs the steps in order
src/interface/<name>/             what a step offers, headers only
src/implementation/<name>/        the code behind an interface
src/error/<name>/                 error types and printing
src/tests/<name>/main.c           one test program per step
```

Tests print `N checks, M failures` and exit non-zero on a failure.
