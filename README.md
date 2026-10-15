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


### Tokenizer


