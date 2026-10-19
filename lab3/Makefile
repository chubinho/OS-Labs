CC = cc
CPPFLAGS = -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE
CFLAGS = -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror
LDLIBS = -pthread
SOURCES = src/main.c src/input.c src/sort.c
HEADERS = src/input.h src/sort.h

.PHONY: all clean
all: build/mergesort

build:
	mkdir -p build

build/mergesort: $(SOURCES) $(HEADERS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) $(LDLIBS) -o $@

clean:
	rm -rf build
