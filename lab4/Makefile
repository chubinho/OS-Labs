CC = cc
CPPFLAGS = -D_POSIX_C_SOURCE=200809L -D_FILE_OFFSET_BITS=64
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Werror -O2

.PHONY: all clean

all: build/parent build/child

build:
	mkdir -p build

build/parent: src/parent.c src/common.c src/common.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) src/parent.c src/common.c -o $@

build/child: src/child.c src/calculate.c src/common.c src/common.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) src/child.c src/calculate.c src/common.c -o $@

clean:
	rm -rf build
