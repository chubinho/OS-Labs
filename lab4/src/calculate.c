#include "common.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

int calculate(char *line, size_t length, int *result)
{
    int first = 1;
    int zero = 0;
    int overflow = 0;

    if (memchr(line, '\0', length) != NULL) {
        return INVALID;
    }
    char *token = strtok(line, " \t\r\n\v\f");
    while (token != NULL) {
        char *end;
        errno = 0;
        long number = strtol(token, &end, 10);
        if (*end != '\0' || errno == ERANGE ||
            number < INT_MIN || number > INT_MAX) {
            return INVALID;
        }
        if (first) {
            *result = (int)number;
            first = 0;
        } else if (number == 0) {
            zero = 1;
        } else if (!zero && !overflow) {
            if (*result == INT_MIN && number == -1) {
                overflow = 1;
            } else {
                *result /= (int)number;
            }
        }
        token = strtok(NULL, " \t\r\n\v\f");
    }
    if (first) {
        return INVALID;
    }
    if (zero) {
        return ZERO;
    }
    if (overflow) {
        return INVALID;
    }
    return DONE;
}
