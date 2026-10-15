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

.PHONY: build clean format run test release

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
	$(CC) $(CFLAGS) -Isrc $(LIB_SOURCES) $(TEST_SOURCES) -o $(BUILD_DIR)/tests
	$(BUILD_DIR)/tests

clean:
	rm -rf $(BUILD_DIR)

release: CFLAGS += -O2 -DNDEBUG
release: clean format
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -Isrc $(SOURCES) -o $(BUILD_DIR)/$(TARGET)

	
# end
