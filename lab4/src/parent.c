#include "common.h"

#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

static pid_t child = -1;
static int child_status = 0;

static int check_child(void)
{
    if (child == -1) {
        return -1;
    }
    pid_t result = waitpid(child, &child_status, WNOHANG);
    if (result == -1) {
        if (errno == EINTR) {
            return 0;
        }
        perror("waitpid");
        return -1;
    }
    if (result == child) {
        child = -1;
        fprintf(stderr, "[parent PID=%ld] child terminated\n", (long)getpid());
        return -1;
    }
    return 0;
}

static int wait_reply(void)
{
    while (!stopped) {
        int result = wait_signal(SIGUSR2, child);
        if (result == 0) {
            return 0;
        }
        if (result == -1 || check_child() == -1) {
            return -1;
        }
    }
    return -1;
}

static int read_line(char **line, size_t *length)
{
    static char buffer[4096];
    static size_t position = 0;
    static size_t available = 0;
    size_t capacity = 128;
    char *text = malloc(capacity);
    *length = 0;
    *line = NULL;
    if (text == NULL) {
        perror("malloc");
        return -1;
    }
    while (!stopped) {
        if (position == available) {
            if (child != -1 && check_child() == -1) {
                free(text);
                return -1;
            }
            struct pollfd input = {STDIN_FILENO, POLLIN, 0};
            int ready = poll(&input, 1, 200);
            if (ready == -1) {
                if (errno == EINTR) {
                    continue;
                }
                perror("poll input");
                break;
            }
            if (ready == 0) {
                continue;
            }
            ssize_t count = read(STDIN_FILENO, buffer, sizeof(buffer));
            if (count == -1) {
                if (errno == EINTR) {
                    continue;
                }
                perror("read input");
                break;
            }
            if (count == 0) {
                text[*length] = '\0';
                *line = text;
                return *length != 0;
            }
            available = (size_t)count;
            position = 0;
        }
        char symbol = buffer[position++];
        if (symbol == '\n') {
            text[*length] = '\0';
            *line = text;
            return 1;
        }
        if (*length == capacity - 1) {
            if (capacity > SIZE_MAX / 2) {
                fprintf(stderr, "Input is too large\n");
                break;
            }
            size_t new_capacity = capacity * 2;
            char *new_text = realloc(text, new_capacity);
            if (new_text == NULL) {
                perror("realloc");
                break;
            }
            text = new_text;
            capacity = new_capacity;
        }
        text[(*length)++] = symbol;
    }
    free(text);
    return -1;
}

static int send_line(int fd, const char *line, size_t length)
{
    if (length > (size_t)LLONG_MAX - sizeof(struct message) - 1 ||
        length > SIZE_MAX - sizeof(struct message) - 1) {
        fprintf(stderr, "Message is too large\n");
        return -1;
    }
    size_t size = sizeof(struct message) + length + 1;
    if (ftruncate(fd, (off_t)size) == -1) {
        perror("ftruncate message");
        return -1;
    }
    struct message *message = mmap(NULL, size, PROT_READ | PROT_WRITE,
                                   MAP_SHARED, fd, 0);
    if (message == MAP_FAILED) {
        perror("mmap message");
        return -1;
    }
    message->status = READY;
    memcpy(message->text, line, length + 1);
    if (unmap_file(message, size) == -1) {
        return -1;
    }
    if (kill(child, SIGUSR1) == -1) {
        perror("signal child");
        return -1;
    }
    return wait_reply();
}

int main(int argc, char **argv)
{
    int fd = -1;
    int exit_code = 1;
    int normal_finish = 0;
    char temporary[] = "/tmp/lab4-XXXXXX";
    int named_file = 0;
    char *line = NULL;
    size_t length = 0;
    struct message *header = MAP_FAILED;

    if (argc > 2) {
        fprintf(stderr, "Usage: parent [CHILD_PROGRAM]\n");
        return 1;
    }
    if (prepare_signals() == -1) {
        goto cleanup;
    }
    fprintf(stderr, "Output file (first line):\n");
    int input = read_line(&line, &length);
    if (input != 1 || length == 0 || memchr(line, '\0', length) != NULL) {
        fprintf(stderr, "Invalid output filename\n");
        goto cleanup;
    }
    fd = mkstemp(temporary);
    if (fd == -1) {
        perror("mkstemp");
        goto cleanup;
    }
    named_file = 1;
    if (unlink(temporary) == -1) {
        perror("unlink mapping file");
        goto cleanup;
    }
    named_file = 0;
    if (ftruncate(fd, sizeof(*header)) == -1) {
        perror("ftruncate header");
        goto cleanup;
    }
    header = mmap(NULL, sizeof(*header), PROT_READ | PROT_WRITE,
                  MAP_SHARED, fd, 0);
    if (header == MAP_FAILED) {
        perror("mmap header");
        goto cleanup;
    }
    char descriptor[32];
    int count = snprintf(descriptor, sizeof(descriptor), "%d", fd);
    if (count < 0 || (size_t)count >= sizeof(descriptor)) {
        fprintf(stderr, "Cannot format descriptor\n");
        goto cleanup;
    }
    char parent_pid[32];
    count = snprintf(parent_pid, sizeof(parent_pid), "%ld", (long)getpid());
    if (count < 0 || (size_t)count >= sizeof(parent_pid)) {
        fprintf(stderr, "Cannot format parent PID\n");
        goto cleanup;
    }
    child = fork();
    if (child == -1) {
        perror("fork");
        goto cleanup;
    }
    if (child == 0) {
        const char *program = argc == 2 ? argv[1] : "./build/child";
        execl(program, program, line, descriptor, parent_pid, (char *)NULL);
        perror("exec child");
        _exit(1);
    }
    fprintf(stderr, "[parent PID=%ld] child PID=%ld\n",
            (long)getpid(), (long)child);
    free(line);
    line = NULL;
    if (wait_reply() == -1) {
        goto cleanup;
    }
    fprintf(stderr, "Enter integer commands; EOF to finish:\n");
    while (!stopped) {
        input = read_line(&line, &length);
        if (input == -1) {
            goto cleanup;
        }
        if (input == 0) {
            header->status = FINISH;
            if (kill(child, SIGUSR1) == -1) {
                perror("signal finish");
                goto cleanup;
            }
            normal_finish = 1;
            exit_code = 0;
            break;
        }
        if (send_line(fd, line, length) == -1) {
            goto cleanup;
        }
        free(line);
        line = NULL;
        if (header->status == ZERO) {
            exit_code = 2;
            normal_finish = 1;
            break;
        }
    }

cleanup:
    free(line);
    if (child > 0) {
        if (!normal_finish && kill(child, SIGTERM) == -1 && errno != ESRCH) {
            perror("terminate child");
            exit_code = 1;
        }
        pid_t result;
        do {
            result = waitpid(child, &child_status, 0);
        } while (result == -1 && errno == EINTR);
        if (result == -1) {
            perror("waitpid");
            exit_code = 1;
        } else if (WIFEXITED(child_status) && WEXITSTATUS(child_status) == 2) {
            exit_code = 2;
        } else if (!WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0) {
            exit_code = 1;
        }
    }
    if (unmap_file(header, sizeof(*header)) == -1) {
        exit_code = 1;
    }
    if (close_file(fd) == -1) {
        exit_code = 1;
    }
    if (named_file && unlink(temporary) == -1) {
        perror("unlink cleanup");
        exit_code = 1;
    }
    if (stopped) {
        exit_code = 128 + stopped;
    }
    return exit_code;
}
