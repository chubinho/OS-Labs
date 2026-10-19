#ifndef SORT_H
#define SORT_H

#include <stddef.h>

/* The limit includes the main thread. scratch has count elements. */
int parallel_sort(int *array, int *scratch, size_t count, unsigned limit,
                  int verbose, unsigned hold_seconds, unsigned *peak);

#endif
