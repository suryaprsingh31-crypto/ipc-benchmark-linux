#include "benchmark_common.h"
#include <fcntl.h>
#include <linux/limits.h>

int main(int argc, char **argv) {
    bench_config cfg;
    if (parse_config(argc, argv, &cfg)) return 2;
    int data[2], ready[2], start[2];
    if (pipe(data) || pipe(ready) || pipe(start)) { perror("pipe"); return 1; }
    if (cfg.capacity * cfg.message_size <= (size_t)INT_MAX)
        (void)fcntl(data[1], F_SETPIPE_SZ, (int)(cfg.capacity * cfg.message_size));
    int pipe_bytes = fcntl(data[1], F_GETPIPE_SZ);
    cfg.actual_capacity_bytes = pipe_bytes > 0 ? (size_t)pipe_bytes : 0;
    char *buffer = malloc(cfg.message_size);
    if (!buffer) { perror("malloc"); return 1; }
    memset(buffer, 'A', cfg.message_size);
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); free(buffer); return 1; }
    if (pid == 0) {
        close(data[1]); close(ready[0]); close(start[1]);
        /* Fault in the consumer's private pages before the timed interval. */
        memset(buffer, 0, cfg.message_size);
        char token = 'R';
        if (write_all(ready[1], &token, 1)) _exit(2);
        close(ready[1]);
        if (read_all(start[0], &token, 1)) _exit(2);
        close(start[0]);
        for (uint64_t i = 0; i < cfg.messages; ++i) {
            if (read_all(data[0], buffer, cfg.message_size)) { perror("pipe read"); _exit(3); }
            for (size_t j = 0; j < cfg.message_size; ++j) if (buffer[j] != 'A') _exit(4);
        }
        close(data[0]); _exit(0);
    }
    close(data[0]); close(ready[1]); close(start[0]);
    char token;
    if (read_all(ready[0], &token, 1)) { perror("consumer ready"); return 1; }
    close(ready[0]);
    double t0 = monotonic_seconds();
    token = 'G';
    if (write_all(start[1], &token, 1)) { perror("start consumer"); return 1; }
    close(start[1]);
    int rc = 0;
    for (uint64_t i = 0; i < cfg.messages; ++i)
        if (write_all(data[1], buffer, cfg.message_size)) { perror("pipe write"); rc = 1; break; }
    close(data[1]);
    if (wait_success(pid)) rc = 1;
    double elapsed = monotonic_seconds() - t0;
    free(buffer);
    if (rc) return 1;
    report_result("pipe", &cfg, elapsed);
    return 0;
}
