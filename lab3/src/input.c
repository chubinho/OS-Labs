#include "input.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_unsigned(const char *text, unsigned long long maximum,
                          unsigned long long *value)
{
    if (*text == '\0') {
        return -1;
    }
    for (const char *p = text; *p != '\0'; ++p) {
        if (!isdigit((unsigned char)*p)) {
            return -1;
        }
    }
    errno = 0;
    char *end;
    unsigned long long parsed = strtoull(text, &end, 10);
    if (errno == ERANGE || *end != '\0' || parsed > maximum) {
        return -1;
    }
    *value = parsed;
    return 0;
}

static void usage(void)
{
    puts("Usage: mergesort -t THREADS (-n COUNT | -i FILE) [options]\n"
         "  -t THREADS      maximum live threads INCLUDING main (1..256)\n"
         "  -n COUNT        generate COUNT integers (0 is allowed)\n"
         "  -i FILE         read whitespace-separated integers until EOF\n"
         "  -s SEED         deterministic generator seed (0..4294967295)\n"
         "  --kind KIND     random, sorted, reverse, equal (default random)\n"
         "  --verbose       print PID, native TID and live counter\n"
         "  --print         print full sorted array after timing\n"
         "  --hold SECONDS  hold leaf threads for OS inspection (0..60)\n"
         "  --help          show this help");
}

int parse_options(int argc, char **argv, struct options *options)
{
    *options = (struct options){0};
    options->seed = 42;
    options->kind = "random";
    int have_count = 0;
    int have_threads = 0;
    for (int i = 1; i < argc; ++i) {
        const char *key = argv[i];
        if (strcmp(key, "--help") == 0) {
            usage();
            return 1;
        }
        if (strcmp(key, "--verbose") == 0) {
            options->verbose = 1;
            continue;
        }
        if (strcmp(key, "--print") == 0) {
            options->print_array = 1;
            continue;
        }
        if (i + 1 == argc) {
            fprintf(stderr, "Missing value for %s\n", key);
            return -1;
        }
        const char *value = argv[++i];
        if (strcmp(key, "-i") == 0) {
            if (options->input != NULL) {
                fprintf(stderr, "Duplicate -i\n");
                return -1;
            }
            options->input = value;
            continue;
        }
        if (strcmp(key, "--kind") == 0) {
            if (strcmp(value, "random") != 0 && strcmp(value, "sorted") != 0 &&
                strcmp(value, "reverse") != 0 && strcmp(value, "equal") != 0) {
                fprintf(stderr, "Unknown input kind: %s\n", value);
                return -1;
            }
            options->kind = value;
            continue;
        }
        unsigned long long number;
        unsigned long long maximum = UINT32_MAX;
        if (strcmp(key, "-n") == 0) {
            maximum = SIZE_MAX / sizeof(int);
        } else if (strcmp(key, "-t") == 0) {
            maximum = 256;
        } else if (strcmp(key, "--hold") == 0) {
            maximum = 60;
        } else if (strcmp(key, "-s") != 0) {
            fprintf(stderr, "Unknown option: %s\n", key);
            return -1;
        }
        if (parse_unsigned(value, maximum, &number) != 0) {
            fprintf(stderr, "Invalid value for %s: %s\n", key, value);
            return -1;
        }
        if (strcmp(key, "-n") == 0) {
            if (have_count) {
                fprintf(stderr, "Duplicate -n\n");
                return -1;
            }
            options->count = (size_t)number;
            have_count = 1;
        } else if (strcmp(key, "-t") == 0) {
            if (have_threads || number == 0) {
                fprintf(stderr, "-t must occur once and be in 1..256\n");
                return -1;
            }
            options->threads = (unsigned)number;
            have_threads = 1;
        } else if (strcmp(key, "-s") == 0) {
            options->seed = (uint32_t)number;
        } else {
            options->hold_seconds = (unsigned)number;
        }
    }
    if (!have_threads || have_count == (options->input != NULL)) {
        fprintf(stderr, "Specify -t and exactly one of -n, -i. See --help.\n");
        return -1;
    }
    return 0;
}

int read_array(const char *path, int **array, size_t *count)
{
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        perror("fopen");
        return -1;
    }
    size_t capacity = 0;
    *array = NULL;
    *count = 0;
    int result = -1;
    int ch;
    while ((ch = fgetc(file)) != EOF) {
        if (isspace((unsigned char)ch)) {
            continue;
        }
        char token[64];
        size_t length = 0;
        do {
            if (length + 1 == sizeof(token)) {
                fprintf(stderr, "Input integer token is too long\n");
                goto cleanup;
            }
            token[length++] = (char)ch;
            ch = fgetc(file);
        } while (ch != EOF && !isspace((unsigned char)ch));
        token[length] = '\0';
        errno = 0;
        char *end;
        long value = strtol(token, &end, 10);
        if (errno == ERANGE || end == token || *end != '\0' ||
            value < INT_MIN || value > INT_MAX || memchr(token, '\0', length)) {
            fprintf(stderr, "Invalid integer in input\n");
            goto cleanup;
        }
        if (*count == capacity) {
            size_t maximum = SIZE_MAX / sizeof(int);
            if (capacity == maximum) {
                fprintf(stderr, "Input array is too large\n");
                goto cleanup;
            }
            size_t next = 1024;
            if (capacity != 0) {
                next = capacity > maximum / 2 ? maximum : capacity * 2;
            }
            int *grown = realloc(*array, next * sizeof(int));
            if (grown == NULL) {
                perror("realloc");
                goto cleanup;
            }
            *array = grown;
            capacity = next;
        }
        (*array)[(*count)++] = (int)value;
    }
    if (ferror(file)) {
        perror("read input");
        goto cleanup;
    }
    result = 0;
cleanup:
    if (fclose(file) == EOF) {
        perror("fclose");
        result = -1;
    }
    if (result != 0) {
        free(*array);
        *array = NULL;
        *count = 0;
    }
    return result;
}

void generate_array(int *array, size_t count, uint32_t seed, const char *kind)
{
    uint32_t state = seed;
    for (size_t i = 0; i < count; ++i) {
        /* Unsigned overflow is defined modulo 2^32. */
        state = state * UINT32_C(1664525) + UINT32_C(1013904223);
        array[i] = (int)(state % UINT32_C(2000001)) - 1000000;
        if (strcmp(kind, "sorted") == 0) {
            array[i] = (int)(i / (count / 2000001 + 1)) - 1000000;
        } else if (strcmp(kind, "reverse") == 0) {
            array[i] = 1000000 - (int)(i / (count / 2000001 + 1));
        } else if (strcmp(kind, "equal") == 0) {
            array[i] = 7;
        }
    }
}

int print_array(const int *array, size_t count)
{
    if (printf("ARRAY:") < 0) {
        return -1;
    }
    for (size_t i = 0; i < count; ++i) {
        if (printf(" %d", array[i]) < 0) {
            return -1;
        }
    }
    return putchar('\n') == EOF ? -1 : 0;
}
