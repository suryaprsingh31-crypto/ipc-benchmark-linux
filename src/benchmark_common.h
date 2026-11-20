#ifndef BENCHMARK_COMMON_H
#define BENCHMARK_COMMON_H

#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

typedef struct {
    uint64_t messages;
    size_t message_size;
    size_t capacity;
    size_t actual_capacity_bytes;
} bench_config;

static inline void usage(const char *program) {
    fprintf(stderr, "Usage: %s [messages [message_size_bytes [capacity_messages]]]\n", program);
}

static inline int parse_u64(const char *s, uint64_t *out) {
    char *end = NULL;
    if (!s[0]) return -1;
    for (const char *p = s; *p; ++p) if (*p < '0' || *p > '9') return -1;
    errno = 0;
    unsigned long long v = strtoull(s, &end, 10);
    if (errno || !s[0] || !end || *end || v == 0) return -1;
    *out = (uint64_t)v;
    return 0;
}

static inline int parse_config(int argc, char **argv, bench_config *cfg) {
    cfg->messages = 100000;
    cfg->message_size = 64;
    cfg->capacity = 8;
    uint64_t v;
    if (argc > 4) { usage(argv[0]); return -1; }
    if (argc > 1) { if (parse_u64(argv[1], &cfg->messages)) goto invalid; }
    if (argc > 2) {
        if (parse_u64(argv[2], &v) || v > SIZE_MAX) goto invalid;
        cfg->message_size = (size_t)v;
    }
    if (argc > 3) {
        if (parse_u64(argv[3], &v) || v > SIZE_MAX) goto invalid;
        cfg->capacity = (size_t)v;
    }
    if (cfg->message_size > (size_t)SSIZE_MAX || cfg->message_size > (size_t)LONG_MAX || cfg->capacity > (size_t)LONG_MAX ||
        cfg->messages > UINT64_MAX / cfg->message_size ||
        cfg->capacity > SIZE_MAX / cfg->message_size) goto invalid;
    return 0;
invalid:
    fprintf(stderr, "All arguments must be positive integers within supported size limits.\n");
    usage(argv[0]);
    return -1;
}

static inline double monotonic_seconds(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) { perror("clock_gettime"); exit(1); }
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static inline int write_all(int fd, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    while (len) {
        ssize_t n = write(fd, p, len);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        p += n; len -= (size_t)n;
    }
    return 0;
}

static inline int read_all(int fd, void *buf, size_t len) {
    char *p = (char *)buf;
    while (len) {
        ssize_t n = read(fd, p, len);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        p += n; len -= (size_t)n;
    }
    return 0;
}

static inline int wait_success(pid_t pid) {
    int status;
    while (waitpid(pid, &status, 0) < 0) if (errno != EINTR) { perror("waitpid"); return -1; }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "Consumer process failed.\n"); return -1;
    }
    return 0;
}

static inline void report_result(const char *method, const bench_config *cfg, double elapsed) {
    double bytes = (double)cfg->messages * (double)cfg->message_size;
    printf("%s,\"%" PRIu64 "\",%zu,%zu,%zu,%.9f,%.3f,%.3f\n",
           method, cfg->messages, cfg->message_size, cfg->capacity, cfg->actual_capacity_bytes, elapsed,
           (bytes / (1024.0 * 1024.0)) / elapsed, (double)cfg->messages / elapsed);
}

#endif
