#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>

enum {
    READY = 'R',
    DONE = 'O',
    INVALID = 'I',
    ZERO = 'Z'
};

int write_all(int fd, const void *data, size_t size);
int read_status(int fd);
int close_fd(int *fd);

#endif
