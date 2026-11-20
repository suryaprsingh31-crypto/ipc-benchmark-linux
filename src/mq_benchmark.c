#include "benchmark_common.h"
#include <fcntl.h>
#include <mqueue.h>
#include <sys/stat.h>

int main(int argc, char **argv) {
    bench_config cfg;
    int config_status = parse_config(argc, argv, &cfg);
    if (config_status) return config_status > 0 ? 0 : 2;
    char name[64];
    snprintf(name, sizeof(name), "/ipcbench_%ld", (long)getpid());
    struct mq_attr attr = {0};
    attr.mq_maxmsg = (long)cfg.capacity;
    attr.mq_msgsize = (long)cfg.message_size;
    mqd_t q = mq_open(name, O_CREAT | O_EXCL | O_RDWR, 0600, &attr);
    if (q == (mqd_t)-1) { perror("mq_open (check /proc/sys/fs/mqueue limits)"); return 1; }
    struct mq_attr actual;
    if (mq_getattr(q, &actual) == 0) cfg.actual_capacity_bytes = (size_t)actual.mq_maxmsg * (size_t)actual.mq_msgsize;
    int ready[2], start[2];
    if (pipe(ready) || pipe(start)) { perror("pipe"); mq_close(q); mq_unlink(name); return 1; }
    char *buffer = malloc(cfg.message_size);
    if (!buffer) { perror("malloc"); mq_close(q); mq_unlink(name); return 1; }
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); free(buffer); mq_close(q); mq_unlink(name); return 1; }
    if (pid == 0) {
        close(ready[0]); close(start[1]);
        /* Fault in the consumer's private pages before the timed interval. */
        memset(buffer, 0, cfg.message_size);
        char token = 'R';
        if (write_all(ready[1], &token, 1)) _exit(2);
        close(ready[1]);
        if (read_all(start[0], &token, 1)) _exit(2);
        close(start[0]);
        for (uint64_t i = 0; i < cfg.messages; ++i) {
            ssize_t n;
            do { n = mq_receive(q, buffer, cfg.message_size, NULL); } while (n < 0 && errno == EINTR);
            if (n < 0 || (size_t)n != cfg.message_size) { perror("mq_receive"); _exit(4); }
            for (size_t j = 0; j < cfg.message_size; ++j) if (buffer[j] != 'A') _exit(5);
        }
        mq_close(q); _exit(0);
    }
    close(ready[1]); close(start[0]);
    char token;
    if (read_all(ready[0], &token, 1)) { perror("consumer ready"); return 1; }
    close(ready[0]);
    memset(buffer, 'A', cfg.message_size);
    double t0 = monotonic_seconds(); token = 'G';
    if (write_all(start[1], &token, 1)) { perror("start consumer"); return 1; }
    close(start[1]);
    int rc = 0;
    for (uint64_t i = 0; i < cfg.messages && !rc; ++i)
        while (mq_send(q, buffer, cfg.message_size, 0) < 0) {
            if (errno == EINTR) continue;
            perror("mq_send"); rc = 1; break;
        }
    if (mq_close(q)) { perror("mq_close"); rc = 1; }
    if (wait_success(pid)) rc = 1;
    double elapsed = monotonic_seconds() - t0;
    free(buffer);
    if (mq_unlink(name)) { perror("mq_unlink"); rc = 1; }
    if (rc) return 1;
    report_result("message_queue", &cfg, elapsed);
    return 0;
}
