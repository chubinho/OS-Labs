#include "common.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    int fd = -1;
    int output = -1;
    int exit_code = 1;
    struct message *header = MAP_FAILED;
    struct message *message = MAP_FAILED;
    size_t size = 0;
    size_t line = 0;
    pid_t parent = -1;

    if (argc != 4) {
        fprintf(stderr, "Usage: child OUTPUT_FILE MAPPING_FD PARENT_PID\n");
        return 1;
    }
    char *end;
    errno = 0;
    long value = strtol(argv[2], &end, 10);
    if (end == argv[2] || *end != '\0' || errno == ERANGE ||
        value < 0 || value > INT_MAX) {
        fprintf(stderr, "Invalid mapping descriptor\n");
        return 1;
    }
    fd = (int)value;
    errno = 0;
    value = strtol(argv[3], &end, 10);
    if (end == argv[3] || *end != '\0' || errno == ERANGE ||
        value <= 1 || value > INT_MAX) {
        fprintf(stderr, "Invalid parent PID\n");
        goto cleanup;
    }
    parent = (pid_t)value;
    if (getppid() != parent) {
        fprintf(stderr, "Parent process terminated\n");
        goto cleanup;
    }
    if (prepare_signals() == -1) {
        goto cleanup;
    }
    struct stat file_info;
    if (fstat(fd, &file_info) == -1) {
        perror("fstat");
        goto cleanup;
    }
    if (file_info.st_size < (off_t)sizeof(*header)) {
        fprintf(stderr, "Invalid mapping size\n");
        goto cleanup;
    }
    header = mmap(NULL, sizeof(*header), PROT_READ | PROT_WRITE,
                  MAP_SHARED, fd, 0);
    if (header == MAP_FAILED) {
        perror("mmap header");
        goto cleanup;
    }
    output = open(argv[1], O_WRONLY | O_CREAT | O_NONBLOCK, 0666);
    if (output == -1) {
        perror("open output");
        goto cleanup;
    }
    if (fstat(output, &file_info) == -1) {
        perror("fstat output");
        goto cleanup;
    }
    if (!S_ISREG(file_info.st_mode)) {
        fprintf(stderr, "Output must be a regular file\n");
        goto cleanup;
    }
    if (ftruncate(output, 0) == -1) {
        perror("ftruncate output");
        goto cleanup;
    }
    fprintf(stderr, "[child PID=%ld] ready\n", (long)getpid());
    header->status = READY;
    if (kill(parent, SIGUSR2) == -1) {
        perror("signal parent");
        goto cleanup;
    }
    while (!stopped) {
        if (wait_signal(SIGUSR1, parent) == -1) {
            goto cleanup;
        }
        if (header->status == FINISH) {
            exit_code = 0;
            break;
        }
        if (fstat(fd, &file_info) == -1) {
            perror("fstat");
            goto cleanup;
        }
        if (file_info.st_size <= (off_t)sizeof(*header) ||
            (uintmax_t)file_info.st_size > SIZE_MAX) {
            fprintf(stderr, "Invalid message size\n");
            goto cleanup;
        }
        size = (size_t)file_info.st_size;
        message = mmap(NULL, size, PROT_READ | PROT_WRITE,
                       MAP_SHARED, fd, 0);
        if (message == MAP_FAILED) {
            perror("mmap message");
            goto cleanup;
        }
        if (((char *)message)[size - 1] != '\0') {
            fprintf(stderr, "Unterminated message\n");
            goto cleanup;
        }
        line++;
        int result = 0;
        int status = calculate(message->text,
                               size - sizeof(*message) - 1, &result);
        if (status == DONE) {
            if (dprintf(output, "%d\n", result) < 0) {
                perror("write result");
                goto cleanup;
            }
            fprintf(stderr, "[child PID=%ld] line=%zu result=%d\n",
                    (long)getpid(), line, result);
        } else if (status == ZERO) {
            fprintf(stderr, "[child PID=%ld] line=%zu division by zero\n",
                    (long)getpid(), line);
        } else {
            fprintf(stderr, "[child PID=%ld] line=%zu invalid command\n",
                    (long)getpid(), line);
        }
        header->status = status;
        header->result = result;
        if (unmap_file(message, size) == -1) {
            message = MAP_FAILED;
            goto cleanup;
        }
        message = MAP_FAILED;
        if (kill(parent, SIGUSR2) == -1) {
            perror("signal parent");
            goto cleanup;
        }
        if (status == ZERO) {
            exit_code = 2;
            break;
        }
    }

cleanup:
    if (unmap_file(message, size) == -1) {
        exit_code = 1;
    }
    if (unmap_file(header, sizeof(*header)) == -1) {
        exit_code = 1;
    }
    if (close_file(output) == -1) {
        exit_code = 1;
    }
    if (close_file(fd) == -1) {
        exit_code = 1;
    }
    return exit_code;
}
