#include "input.h"
#include "sort.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static int now(struct timespec *time)
{
    if (clock_gettime(CLOCK_MONOTONIC, time) == -1) {
        perror("clock_gettime");
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    struct options options;
    int parsed = parse_options(argc, argv, &options);
    if (parsed != 0) {
        return parsed > 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    int *array = NULL;
    int *scratch = NULL;
    int result = EXIT_FAILURE;
    size_t count = options.count;
    if (options.input != NULL) {
        if (read_array(options.input, &array, &count) != 0) {
            goto cleanup;
        }
    } else if (count != 0) {
        array = malloc(count * sizeof(int));
        if (array == NULL) {
            perror("malloc array");
            goto cleanup;
        }
        generate_array(array, count, options.seed, options.kind);
    }
    if (count != 0) {
        scratch = malloc(count * sizeof(int));
        if (scratch == NULL) {
            perror("malloc scratch");
            goto cleanup;
        }
    }

    struct timespec start, finish;
    unsigned peak;
    if (now(&start) != 0) {
        goto cleanup;
    }
    if (parallel_sort(array, scratch, count, options.threads, options.verbose,
                      options.hold_seconds, &peak) != 0) {
        goto cleanup;
    }
    if (now(&finish) != 0) {
        goto cleanup;
    }
    double seconds = (double)(finish.tv_sec - start.tv_sec) +
                     (double)(finish.tv_nsec - start.tv_nsec) / 1000000000.0;
    for (size_t i = 1; i < count; ++i) {
        if (array[i - 1] > array[i]) {
            fprintf(stderr, "Verification failed at index %zu\n", i);
            goto cleanup;
        }
    }
    /* A checksum helps compare runs; tests also compare every element. */
    uint64_t checksum = 0;
    for (size_t i = 0; i < count; ++i) {
        checksum = checksum * UINT64_C(31) + (uint32_t)array[i];
    }
    if (printf("PID=%ld n=%zu limit=%u peak=%u\n"
               "seconds=%.9f sorted=OK checksum=%" PRIu64 "\n",
               (long)getpid(), count, options.threads, peak,
               seconds, checksum) < 0) {
        perror("stdout");
        goto cleanup;
    }
    if (options.print_array && print_array(array, count) != 0) {
        perror("print array");
        goto cleanup;
    }
    if (fflush(stdout) == EOF) {
        perror("fflush stdout");
        goto cleanup;
    }
    result = EXIT_SUCCESS;
cleanup:
    free(scratch);
    free(array);
    return result;
}
