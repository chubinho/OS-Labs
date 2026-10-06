#include "common.h"

#include <errno.h>
#include <stdio.h>
#include <unistd.h>

int write_all(int fd, const void *data, size_t size)
{
    const char *buffer = data;
    size_t bytes_left = size;

    while (bytes_left > 0) {
        ssize_t bytes_written = write(fd, buffer, bytes_left);

        if (bytes_written == -1) {

            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        if (bytes_written == 0) {
            errno = EIO;
            return -1;
        }

        buffer += bytes_written;
        bytes_left -= (size_t)bytes_written;
    }

    return 0;
}

int read_status(int fd)
{
    unsigned char status;

    while (1) {
        ssize_t bytes_read = read(fd, &status, 1);

        if (bytes_read == 1) {
            return status;
        }

        if (bytes_read == 0) {
            errno = EPIPE;
            return -1;
        }

        if (errno != EINTR) {
            return -1;
        }
    }
}

int close_fd(int *fd)
{
    if (*fd == -1) {
        return 0;
    }

    int descriptor = *fd;
    *fd = -1;

    if (close(descriptor) == -1) {
        perror("close");
        return -1;
    }

    return 0;
}
