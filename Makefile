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
SOURCES := $(shell find src -type f -name '*.c')
HEADERS := $(shell find src -type f -name '*.h')
OBJECTS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SOURCES))
DEPFILES := $(OBJECTS:.o=.d)

.PHONY: build clean format run

all: build

format:
	clang-format -i $(SOURCES) $(HEADERS)

build: format
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -Isrc $(SOURCES) -o $(BUILD_DIR)/$(TARGET)

run: build
	$(BUILD_DIR)/$(TARGET)

clean:
	rm -rf $(BUILD_DIR)

	
# end
