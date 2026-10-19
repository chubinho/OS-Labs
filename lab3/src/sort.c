#include "sort.h"

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/syscall.h>
#endif

/* Small ranges are sorted without creating more threads. */
#define PARALLEL_THRESHOLD 2048

struct context {
    int *array;
    int *scratch;
    pthread_mutex_t mutex;
    unsigned live;
    unsigned peak;
    int verbose;
    unsigned hold_seconds;
};

struct task {
    struct context *context;
    size_t begin;
    size_t end;
    unsigned budget;
    int error;
};

static void check_pthread(int error, const char *operation)
{
    if (error != 0) {
        fprintf(stderr, "%s: %s\n", operation, strerror(error));
        /* Synchronization is broken: terminate without freeing live data. */
        exit(EXIT_FAILURE);
    }
}

static uint64_t thread_id(void)
{
#ifdef __APPLE__
    uint64_t id;
    check_pthread(pthread_threadid_np(NULL, &id), "pthread_threadid_np");
    return id;
#elif defined(__linux__)
    long id = syscall(SYS_gettid);
    if (id == -1) {
        perror("gettid");
        exit(EXIT_FAILURE);
    }
    return (uint64_t)id;
#else
#error This program supports Linux and macOS
#endif
}

static void log_event(struct context *context, const char *event)
{
    if (context->verbose) {
        if (printf("PID=%ld TID=%" PRIu64 " %s live=%u\n",
                   (long)getpid(), thread_id(), event, context->live) < 0 ||
            fflush(stdout) == EOF) {
            perror("stdout");
            exit(EXIT_FAILURE);
        }
    }
}

static void merge(struct context *context, size_t begin, size_t middle,
                  size_t end)
{
    size_t left = begin;
    size_t right = middle;
    size_t out = begin;
    int *array = context->array;
    int *scratch = context->scratch;

    while (left < middle && right < end) {
        if (array[left] <= array[right]) {
            scratch[out++] = array[left++];
        } else {
            scratch[out++] = array[right++];
        }
    }
    while (left < middle) {
        scratch[out++] = array[left++];
    }
    while (right < end) {
        scratch[out++] = array[right++];
    }
    for (size_t i = begin; i < end; ++i) {
        array[i] = scratch[i];
    }
}

static void sequential_sort(struct context *context, size_t begin, size_t end)
{
    if (end - begin < 2) {
        return;
    }
    size_t middle = begin + (end - begin) / 2;
    sequential_sort(context, begin, middle);
    sequential_sort(context, middle, end);
    merge(context, begin, middle, end);
}

static int sort_range(struct task *task);

static void *worker(void *argument)
{
    struct task *task = argument;
    struct context *context = task->context;
    check_pthread(pthread_mutex_lock(&context->mutex), "pthread_mutex_lock");
    ++context->live;
    if (context->live > context->peak) {
        context->peak = context->live;
    }
    log_event(context, "start");
    check_pthread(pthread_mutex_unlock(&context->mutex), "pthread_mutex_unlock");

    task->error = sort_range(task);

    check_pthread(pthread_mutex_lock(&context->mutex), "pthread_mutex_lock");
    log_event(context, "finish");
    --context->live;
    check_pthread(pthread_mutex_unlock(&context->mutex), "pthread_mutex_unlock");
    return NULL;
}

static int sort_range(struct task *task)
{
    struct context *context = task->context;
    if (task->budget == 1 || task->end - task->begin < PARALLEL_THRESHOLD) {
        /* Demonstration only; disabled in all benchmarks. */
        if (context->hold_seconds != 0) {
            struct timespec delay = {(time_t)context->hold_seconds, 0};
            while (nanosleep(&delay, &delay) == -1) {
                if (errno != EINTR) {
                    int error = errno;
                    perror("nanosleep");
                    return error;
                }
            }
        }
        sequential_sort(context, task->begin, task->end);
        return 0;
    }

    size_t middle = task->begin + (task->end - task->begin) / 2;
    unsigned left_budget = task->budget / 2;
    struct task left = {context, task->begin, middle, left_budget, 0};
    struct task right = {context, middle, task->end,
                         task->budget - left_budget, 0};
    pthread_t thread;
    int error = pthread_create(&thread, NULL, worker, &left);
    if (error != 0) {
        fprintf(stderr, "pthread_create: %s\n", strerror(error));
        return error;
    }

    int right_error = sort_range(&right);
    /* Join even when the right branch failed. left stays alive until here. */
    check_pthread(pthread_join(thread, NULL), "pthread_join");
    if (left.error != 0) {
        return left.error;
    }
    if (right_error != 0) {
        return right_error;
    }
    merge(context, task->begin, middle, task->end);
    return 0;
}

int parallel_sort(int *array, int *scratch, size_t count, unsigned limit,
                  int verbose, unsigned hold_seconds, unsigned *peak)
{
    struct context context = {0};
    context.array = array;
    context.scratch = scratch;
    context.live = 1;
    context.peak = 1;
    context.verbose = verbose;
    context.hold_seconds = hold_seconds;
    int error = pthread_mutex_init(&context.mutex, NULL);
    if (error != 0) {
        fprintf(stderr, "pthread_mutex_init: %s\n", strerror(error));
        return error;
    }
    log_event(&context, "main");
    struct task root = {&context, 0, count, limit, 0};
    error = sort_range(&root);
    *peak = context.peak;
    check_pthread(pthread_mutex_destroy(&context.mutex), "pthread_mutex_destroy");
    return error;
}
