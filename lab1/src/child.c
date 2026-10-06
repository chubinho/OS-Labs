#include "common.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int calculate(char *line, size_t line_length, int *answer)
{
    int is_first_number = 1;
    int has_zero_divisor = 0;
    int has_overflow = 0;
    const char *separators = " \t\r\n\v\f";

    if (memchr(line, '\0', line_length) != NULL) {
        return INVALID;
    }

    char *token = strtok(line, separators);

    while (token != NULL) {
        char *number_end;

        errno = 0;
        long number = strtol(token, &number_end, 10);

        if (number_end == token || *number_end != '\0') {
            return INVALID;
        }

        if (errno == ERANGE || number < INT_MIN || number > INT_MAX) {
            return INVALID;
        }

        if (is_first_number == 1) {
            *answer = (int)number;
            is_first_number = 0;
        } else if (number == 0) {
            has_zero_divisor = 1;
        } else if (has_zero_divisor == 0 && has_overflow == 0) {

            if (*answer == INT_MIN && number == -1) {
                has_overflow = 1;
            } else {
                *answer = *answer / (int)number;
            }
        }

        token = strtok(NULL, separators);
    }

    if (is_first_number == 1) {
        return INVALID;
    }

    if (has_zero_divisor == 1) {
        return ZERO;
    }

    if (has_overflow == 1) {
        return INVALID;
    }

    return DONE;
}

int main(int argc, char **argv)
{
    int output_file = -1;
    int exit_code = 1;
    char *line = NULL;
    size_t line_capacity = 0;
    size_t line_number = 0;

    if (argc != 2) {
        fprintf(stderr, "Usage: child OUTPUT_FILE\n");
        return 1;
    }

    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        perror("signal");
        goto cleanup;
    }

    output_file = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (output_file == -1) {
        perror("open output file");
        goto cleanup;
    }

    fprintf(stderr, "[child pid=%ld] output=%s\n", (long)getpid(), argv[1]);

    char status = READY;
    if (write_all(STDOUT_FILENO, &status, 1) == -1) {
        perror("send READY");
        goto cleanup;
    }

    exit_code = 0;

    while (1) {
        ssize_t line_length = getline(&line, &line_capacity, stdin);

        if (line_length == -1) {
            if (!feof(stdin)) {
                perror("getline");
                exit_code = 1;
            }
            break;
        }

        line_number++;
        int answer = 0;
        status = (char)calculate(line, (size_t)line_length, &answer);

        if (status == DONE) {
            if (dprintf(output_file, "%d\n", answer) < 0) {
                perror("write output file");
                exit_code = 1;
                break;
            }

            fprintf(stderr, "[child pid=%ld] line=%zu: result=%d\n",
                    (long)getpid(), line_number, answer);
        } else if (status == ZERO) {
            fprintf(stderr, "[child pid=%ld] line=%zu: division by zero\n",
                    (long)getpid(), line_number);
        } else {
            fprintf(stderr, "[child pid=%ld] line=%zu: command rejected\n",
                    (long)getpid(), line_number);
        }

        if (write_all(STDOUT_FILENO, &status, 1) == -1) {
            perror("write pipe");
            exit_code = 1;
            break;
        }

        if (status == ZERO) {
            exit_code = 2;
            break;
        }
    }

cleanup:
    free(line);

    if (close_fd(&output_file) == -1) {
        exit_code = 1;
    }

    return exit_code;
}
