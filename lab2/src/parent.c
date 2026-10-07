#include "common.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char **argv)
{

    int command_pipe[2] = {-1, -1};
    int response_pipe[2] = {-1, -1};
    int exit_code = 1;
    pid_t child_pid = -1;
    char *line = NULL;
    size_t line_capacity = 0;
    const char *child_program = "./build/child";

    if (argc > 2) {
        fprintf(stderr, "Usage: parent [CHILD_EXECUTABLE]\n");
        return 1;
    }

    if (argc == 2) {
        child_program = argv[1];
    }

    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        perror("signal");
        goto cleanup;
    }

    for (int fd = STDIN_FILENO; fd <= STDERR_FILENO; fd++) {
        if (fcntl(fd, F_GETFD) == -1) {
            perror("standard stream");
            goto cleanup;
        }
    }

    fprintf(stderr, "[parent pid=%ld] Enter output filename\n", (long)getpid());

    ssize_t line_length = getline(&line, &line_capacity, stdin);
    if (line_length == -1) {
        if (feof(stdin)) {
            fprintf(stderr, "Filename is required\n");
        } else {
            perror("getline filename");
        }
        goto cleanup;
    }

    if (memchr(line, '\0', (size_t)line_length) != NULL) {
        fprintf(stderr, "Invalid filename\n");
        goto cleanup;
    }

    line[strcspn(line, "\r\n")] = '\0';
    if (line[0] == '\0') {
        fprintf(stderr, "Filename is required\n");
        goto cleanup;
    }

    if (pipe(command_pipe) == -1) {
        perror("pipe");
        goto cleanup;
    }

    if (pipe(response_pipe) == -1) {
        perror("pipe");
        goto cleanup;
    }

    child_pid = fork();
    if (child_pid == -1) {
        perror("fork");
        goto cleanup;
    }

    if (child_pid == 0) {

        if (dup2(command_pipe[0], STDIN_FILENO) == -1) {
            perror("dup2 stdin");
            _exit(1);
        }

        if (dup2(response_pipe[1], STDOUT_FILENO) == -1) {
            perror("dup2 stdout");
            _exit(1);
        }

        int close_failed = 0;
        for (int i = 0; i < 2; i++) {
            if (close_fd(&command_pipe[i]) == -1) {
                close_failed = 1;
            }
            if (close_fd(&response_pipe[i]) == -1) {
                close_failed = 1;
            }
        }

        if (close_failed == 1) {
            _exit(1);
        }

        execl(child_program, "child", line, (char *)NULL);

        perror("execl");
        _exit(1);
    }

    fprintf(stderr, "[parent pid=%ld] created child=%ld\n",
            (long)getpid(), (long)child_pid);

    if (close_fd(&command_pipe[0]) == -1) {
        goto cleanup;
    }

    if (close_fd(&response_pipe[1]) == -1) {
        goto cleanup;
    }

    int response = read_status(response_pipe[0]);
    if (response != READY) {
        if (response == -1) {
            perror("child startup");
        } else {
            fprintf(stderr, "Unknown startup response\n");
        }
        goto cleanup;
    }

    fprintf(stderr, "[parent pid=%ld] child READY; enter commands\n",
            (long)getpid());
    exit_code = 0;

    while (1) {
        line_length = getline(&line, &line_capacity, stdin);
        if (line_length == -1) {
            if (!feof(stdin)) {
                perror("getline");
                exit_code = 1;
            }
            break;
        }

        if (write_all(command_pipe[1], line, (size_t)line_length) == -1) {
            perror("write pipe");
            exit_code = 1;
            break;
        }

        if (line[line_length - 1] != '\n') {
            if (write_all(command_pipe[1], "\n", 1) == -1) {
                perror("write pipe");
                exit_code = 1;
                break;
            }
        }

        response = read_status(response_pipe[0]);
        if (response == ZERO) {
            exit_code = 2;
            break;
        }

        if (response != DONE && response != INVALID) {
            if (response == -1) {
                perror("read pipe");
            } else {
                fprintf(stderr, "Unknown child response\n");
            }
            exit_code = 1;
            break;
        }
    }

cleanup:
    free(line);

    for (int i = 0; i < 2; i++) {
        if (close_fd(&command_pipe[i]) == -1) {
            exit_code = 1;
        }
        if (close_fd(&response_pipe[i]) == -1) {
            exit_code = 1;
        }
    }

    if (child_pid > 0) {

        if (exit_code == 1) {
            if (kill(child_pid, SIGTERM) == -1) {
                if (errno != ESRCH) {
                    perror("kill");
                }
            }
        }

        int child_status;
        pid_t waited_pid;

        while (1) {
            waited_pid = waitpid(child_pid, &child_status, 0);
            if (waited_pid == -1 && errno == EINTR) {
                continue;
            }
            break;
        }

        if (waited_pid == -1) {
            perror("waitpid");
            exit_code = 1;
        } else if (!WIFEXITED(child_status)) {
            exit_code = 1;
        } else {
            int child_exit_code = WEXITSTATUS(child_status);

            fprintf(stderr, "[parent pid=%ld] waitpid reaped child=%ld exit=%d\n",
                    (long)getpid(), (long)child_pid, child_exit_code);

            if (child_exit_code != exit_code) {
                exit_code = 1;
            }
        }
    }

    return exit_code;
}
