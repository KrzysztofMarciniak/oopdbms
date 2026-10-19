##
# oopdbms
#
# @file
# @version 0.1

CC ?= cc
CFLAGS := \
		-std=c99 \
		-Wall	\
		-Wextra \
		-Wpedantic 

TARGET := oopdbms
BUILD_DIR := build
SOURCES := $(shell find src -type f -name '*.c' \
	-not -path 'src/tests/*')

TEST_SOURCES := $(shell find src/tests -type f -name '*.c')
TEST_RUNNER_SOURCE := src/tests/test_runner.c
TEST_OBJECTS := $(patsubst src/tests/%.c,$(BUILD_DIR)/tests/%.o,\
		$(filter-out $(TEST_RUNNER_SOURCE),$(TEST_SOURCES)))
TEST_RUNNER := $(BUILD_DIR)/tests/test_runner
LIB_SOURCES := $(filter-out src/main.c,$(SOURCES))
LIB_OBJECTS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(LIB_SOURCES))
HEADERS := $(shell find src -type f -name '*.h')
OBJECTS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SOURCES))
DEPFILES := $(OBJECTS:.o=.d) $(TEST_OBJECTS:.o=.d)
DOXYGEN ?= doxygen
DOCS_DIR := $(BUILD_DIR)/docs
PREFIX ?= /usr/local
DESTDIR ?=
INSTALL ?= install
RELEASE_DIR := $(BUILD_DIR)/release
RELEASE_BINARY := $(RELEASE_DIR)/$(TARGET)
RELEASE_CFLAGS ?= $(CFLAGS) -O2
RELEASE_OBJECTS := $(patsubst src/%.c,$(RELEASE_DIR)/%.o,$(SOURCES))
RELEASE_DEPFILES := $(RELEASE_OBJECTS:.o=.d)
QUICK_START_DB ?= $(BUILD_DIR)/quick-start.oopdb

.PHONY: build clean docs format install quick_start release run test

all: build

format:
	@clang-format -i $(SOURCES) $(TEST_SOURCES) $(HEADERS)

build: format
	$(MAKE) --no-print-directory $(BUILD_DIR)/$(TARGET)

$(BUILD_DIR)/$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/%.o: src/%.c
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) -MMD -MP -c $< -o $@

$(RELEASE_DIR)/%.o: src/%.c
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) -Isrc $(RELEASE_CFLAGS) -MMD -MP -c $< -o $@

release: format
	$(MAKE) --no-print-directory $(RELEASE_BINARY)

$(RELEASE_BINARY): $(RELEASE_OBJECTS)
	@printf 'Linking optimized release binary: %s\n' "$@"
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

install: release
	$(INSTALL) -d "$(DESTDIR)$(PREFIX)/bin"
	$(INSTALL) -m 755 "$(RELEASE_BINARY)" "$(DESTDIR)$(PREFIX)/bin/$(TARGET)"

quick_start: release
	@set -e; \
	mkdir -p "$(dir $(QUICK_START_DB))"; \
	$(RM) "$(QUICK_START_DB)"; \
	run_demo() { \
		printf '\n%s\n' "$$1"; \
		printf '  $$ %s\n' "$$2"; \
		OOPDB="$(QUICK_START_DB)" "$(RELEASE_BINARY)" "$$2"; \
	}; \
	printf 'Creating a fresh demo database at %s\n' "$(QUICK_START_DB)"; \
	run_demo 'First, define an interface for anything with a name.' \
		'CREATE INTERFACE Named (name text)'; \
	run_demo 'Create a Person class that implements that interface.' \
		'CREATE CLASS Person IMPLEMENTS Named (name text, age integer)'; \
	run_demo 'Extend Person to reuse its fields in a Staff class.' \
		'CREATE CLASS Staff EXTENDS Person (department text)'; \
	run_demo 'Create a Book class with a pointer to its author.' \
		'CREATE CLASS Book (title text, price float, author pointer Person)'; \
	run_demo 'Add two people, including one Staff record.' \
		'NEW Person (name = "Ada Lovelace", age = 36)'; \
	run_demo 'Add a staff member using the inherited Person fields.' \
		'NEW Staff (name = "Grace Hopper", age = 85, department = "research")'; \
	run_demo 'Store a book that points to Person.1.' \
		'NEW Book (title = "The Annotated Turing", price = 24.5, author = Person.1)'; \
	run_demo 'Define a function, then call it on the stored person.' \
		'CREATE FUNCTION Person.years_until_100 (target integer) RETURNS integer AS target - age'; \
	run_demo 'Evaluate years_until_100 for Ada.' \
		'CALL Person.years_until_100 (100) ON 1'; \
	run_demo 'Find books priced below 30, update the price, and view them again.' \
		'FIND Book WHERE price < 30'; \
	run_demo 'Apply the price change.' 'SET Book 1 (price = 19.5)'; \
	run_demo 'Show the updated book record.' 'FIND Book'; \
	run_demo 'Inspect Staff and its inherited fields.' 'DESCRIBE Staff'; \
	run_demo 'List the database classes.' 'LIST CLASSES'; \
	run_demo 'List the database interfaces.' 'LIST INTERFACES'

run: build
	$(BUILD_DIR)/$(TARGET)

test: format
	$(MAKE) --no-print-directory $(TEST_RUNNER)
	$(TEST_RUNNER) $(TEST_ARGS)

$(TEST_RUNNER): $(TEST_RUNNER_SOURCE) $(TEST_OBJECTS) $(LIB_OBJECTS) $(HEADERS)
	mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $(LIB_OBJECTS) $(TEST_OBJECTS) $(TEST_RUNNER_SOURCE) \
		$(LDLIBS) -o $@

$(BUILD_DIR)/tests/%.o: src/tests/%.c
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) -MMD -MP \
		-Dmain=test_$(subst -,_,$(subst /,_,$(basename $(patsubst src/tests/%,%,$<)))) \
		-c $< -o $@

docs:
	mkdir -p $(DOCS_DIR)
	@printf '%s\n' \
		'PROJECT_NAME = oopdbms' \
		'INPUT = src' \
		'RECURSIVE = YES' \
		'FILE_PATTERNS = *.c *.h' \
		'STRIP_FROM_PATH = src' \
		'OUTPUT_DIRECTORY = $(DOCS_DIR)' \
		'OPTIMIZE_OUTPUT_FOR_C = YES' \
		'EXTRACT_ALL = YES' \
		'EXTRACT_STATIC = YES' \
		'GENERATE_LATEX = NO' \
		'QUIET = YES' \
		| $(DOXYGEN) -
	@echo "docs: $(DOCS_DIR)/html/index.html"

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPFILES) $(RELEASE_DEPFILES)

# end
