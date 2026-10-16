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
SOURCES := $(shell find src -type f -name '*.c' -not -path 'src/tests/*')
TEST_SOURCES := $(shell find src/tests -type f -name '*.c')
LIB_SOURCES := $(filter-out src/main.c,$(SOURCES))
HEADERS := $(shell find src -type f -name '*.h')
OBJECTS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SOURCES))
DEPFILES := $(OBJECTS:.o=.d)

.PHONY: build clean format run test

all: build

format:
	clang-format -i $(SOURCES) $(TEST_SOURCES) $(HEADERS)

build: format
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -Isrc $(SOURCES) -o $(BUILD_DIR)/$(TARGET)

run: build
	$(BUILD_DIR)/$(TARGET)

test: format
	mkdir -p $(BUILD_DIR)
	@for t in $(TEST_SOURCES); do \
		name=$$(echo $$t | tr '/' '_'); \
		$(CC) $(CFLAGS) -Isrc $(LIB_SOURCES) $$t -o $(BUILD_DIR)/$$name || exit 1; \
		$(BUILD_DIR)/$$name || exit 1; \
	done

clean:
	rm -rf $(BUILD_DIR)

	
# end
