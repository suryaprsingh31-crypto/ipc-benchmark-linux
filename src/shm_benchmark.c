#include "benchmark_common.h"
#include <pthread.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    size_t head, tail, count;
    char data[];
} shared_queue;

static int sync_ok(int rc, const char *what) {
    if (!rc) return 0;
    errno = rc; perror(what); return -1;
}

int main(int argc, char **argv) {
    bench_config cfg;
    if (parse_config(argc, argv, &cfg)) return 2;
    size_t slots_bytes = cfg.capacity * cfg.message_size;
    if (slots_bytes > SIZE_MAX - sizeof(shared_queue)) { fprintf(stderr, "Shared-memory size overflow.\n"); return 2; }
    size_t total_size = sizeof(shared_queue) + slots_bytes;
    if (total_size > (size_t)INT64_MAX) { fprintf(stderr, "Shared-memory mapping is too large.\n"); return 2; }
    cfg.actual_capacity_bytes = slots_bytes;
    char name[64]; snprintf(name, sizeof(name), "/ipcbench_%ld", (long)getpid());
    int fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) { perror("shm_open"); return 1; }
    if (ftruncate(fd, (off_t)total_size)) { perror("ftruncate"); shm_unlink(name); close(fd); return 1; }
    shared_queue *q = mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (q == MAP_FAILED) { perror("mmap"); shm_unlink(name); close(fd); return 1; }
    memset(q, 0, total_size);
    pthread_mutexattr_t ma; pthread_condattr_t ca;
    int rc = pthread_mutexattr_init(&ma);
    if (sync_ok(rc, "pthread_mutexattr_init") || sync_ok(pthread_mutexattr_setpshared(&ma, PTHREAD_PROCESS_SHARED), "mutexattr_setpshared") ||
        sync_ok(pthread_condattr_init(&ca), "pthread_condattr_init") || sync_ok(pthread_condattr_setpshared(&ca, PTHREAD_PROCESS_SHARED), "condattr_setpshared") ||
        sync_ok(pthread_mutex_init(&q->mutex, &ma), "pthread_mutex_init") ||
        sync_ok(pthread_cond_init(&q->not_empty, &ca), "pthread_cond_init") || sync_ok(pthread_cond_init(&q->not_full, &ca), "pthread_cond_init")) {
        shm_unlink(name); munmap(q, total_size); close(fd); return 1;
    }
    pthread_mutexattr_destroy(&ma); pthread_condattr_destroy(&ca);
    int ready[2], start[2];
    if (pipe(ready) || pipe(start)) { perror("pipe"); shm_unlink(name); return 1; }
    char *payload = malloc(cfg.message_size);
    char *slot = malloc(cfg.message_size);
    if (!payload || !slot) { perror("malloc"); free(payload); free(slot); shm_unlink(name); return 1; }
    memset(payload, 'A', cfg.message_size);
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); free(payload); free(slot); shm_unlink(name); return 1; }
    if (pid == 0) {
        close(ready[0]); close(start[1]); memset(slot, 0, cfg.message_size); char token = 'R';
        if (write_all(ready[1], &token, 1)) _exit(2);
        close(ready[1]);
        if (read_all(start[0], &token, 1)) _exit(2);
        close(start[0]);
        for (uint64_t i = 0; i < cfg.messages; ++i) {
            if (sync_ok(pthread_mutex_lock(&q->mutex), "mutex_lock")) _exit(4);
            while (q->count == 0) if (sync_ok(pthread_cond_wait(&q->not_empty, &q->mutex), "cond_wait")) _exit(4);
            memcpy(slot, q->data + q->head * cfg.message_size, cfg.message_size);
            q->head = (q->head + 1) % cfg.capacity; --q->count;
            pthread_cond_signal(&q->not_full); pthread_mutex_unlock(&q->mutex);
            for (size_t j = 0; j < cfg.message_size; ++j) if (slot[j] != 'A') _exit(5);
        }
        _exit(0);
    }
    close(ready[1]); close(start[0]); char token;
    if (read_all(ready[0], &token, 1)) { perror("consumer ready"); return 1; }
    close(ready[0]);
    double t0 = monotonic_seconds(); token = 'G';
    if (write_all(start[1], &token, 1)) { perror("start consumer"); return 1; } close(start[1]);
    int failed = 0;
    for (uint64_t i = 0; i < cfg.messages; ++i) {
        if (sync_ok(pthread_mutex_lock(&q->mutex), "mutex_lock")) { failed = 1; break; }
        while (q->count == cfg.capacity) if (sync_ok(pthread_cond_wait(&q->not_full, &q->mutex), "cond_wait")) { failed = 1; break; }
        if (failed) { pthread_mutex_unlock(&q->mutex); break; }
        memcpy(q->data + q->tail * cfg.message_size, payload, cfg.message_size);
        q->tail = (q->tail + 1) % cfg.capacity; ++q->count;
        pthread_cond_signal(&q->not_empty); pthread_mutex_unlock(&q->mutex);
    }
    if (wait_success(pid)) failed = 1;
    double elapsed = monotonic_seconds() - t0;
    free(payload); free(slot);
    pthread_cond_destroy(&q->not_empty); pthread_cond_destroy(&q->not_full); pthread_mutex_destroy(&q->mutex);
    munmap(q, total_size); close(fd);
    if (shm_unlink(name)) { perror("shm_unlink"); failed = 1; }
    if (failed) return 1;
    report_result("shared_memory", &cfg, elapsed);
    return 0;
}
