#include "common.h"

#include <errno.h>
#include <stdio.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

volatile sig_atomic_t stopped = 0;

static void stop_program(int number)
{
    stopped = number;
}

int prepare_signals(void)
{
    sigset_t blocked;
    struct sigaction action = {0};

    if (sigemptyset(&blocked) == -1 ||
        sigaddset(&blocked, SIGUSR1) == -1 ||
        sigaddset(&blocked, SIGUSR2) == -1 ||
        sigprocmask(SIG_BLOCK, &blocked, NULL) == -1) {
        perror("block signals");
        return -1;
    }
    action.sa_handler = stop_program;
    if (sigemptyset(&action.sa_mask) == -1 ||
        sigaction(SIGINT, &action, NULL) == -1 ||
        sigaction(SIGTERM, &action, NULL) == -1) {
        perror("sigaction");
        return -1;
    }
    return 0;
}

int wait_signal(int number, pid_t peer)
{
    sigset_t signals;
    struct timespec timeout = {1, 0};
    siginfo_t info;

    if (sigemptyset(&signals) == -1 ||
        sigaddset(&signals, number) == -1) {
        perror("signal set");
        return -1;
    }
    while (!stopped) {
        int received = sigtimedwait(&signals, &info, &timeout);
        if (received == number && info.si_pid == peer) {
            return 0;
        }
        if (received == -1 && errno != EINTR && errno != EAGAIN) {
            perror("sigtimedwait");
            return -1;
        }
        if (number == SIGUSR1 && getppid() != peer) {
            fprintf(stderr, "Parent process terminated\n");
            return -1;
        }
        if (number == SIGUSR2) {
            return 1;
        }
    }
    return -1;
}

int close_file(int fd)
{
    if (fd != -1 && close(fd) == -1) {
        perror("close");
        return -1;
    }
    return 0;
}

int unmap_file(void *memory, size_t size)
{
    if (memory != MAP_FAILED && munmap(memory, size) == -1) {
        perror("munmap");
        return -1;
    }
    return 0;
}
