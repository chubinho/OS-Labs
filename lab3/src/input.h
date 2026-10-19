#ifndef INPUT_H
#define INPUT_H

#include <stddef.h>
#include <stdint.h>

struct options {
    size_t count;
    unsigned threads;
    uint32_t seed;
    unsigned hold_seconds;
    int verbose;
    int print_array;
    const char *input;
    const char *kind;
};

int parse_options(int argc, char **argv, struct options *options);
int read_array(const char *path, int **array, size_t *count);
void generate_array(int *array, size_t count, uint32_t seed, const char *kind);
int print_array(const int *array, size_t count);

#endif
