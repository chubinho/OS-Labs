CC = cc
BUILD ?= build
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -Isrc
CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror

.PHONY: all clean run
all: $(BUILD)/parent $(BUILD)/child

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/parent: src/parent.c src/common.c src/common.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/parent.c src/common.c $(LDFLAGS) -o $@

$(BUILD)/child: src/child.c src/common.c src/common.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/child.c src/common.c $(LDFLAGS) -o $@

run: all
	./$(BUILD)/parent ./$(BUILD)/child

clean:
	rm -rf build
