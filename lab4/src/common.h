#ifndef COMMON_H
#define COMMON_H

#include <signal.h>
#include <stddef.h>
#include <sys/types.h>

enum { READY, DONE, INVALID, ZERO, FINISH };

struct message {
    int status;
    int result;
    char text[];
};

extern volatile sig_atomic_t stopped;

int prepare_signals(void);
int wait_signal(int number, pid_t peer);
int close_file(int fd);
int unmap_file(void *memory, size_t size);
int calculate(char *line, size_t length, int *result);

#endif
