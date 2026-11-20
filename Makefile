CC = gcc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Wpedantic
LDLIBS_COMMON = -lrt -pthread
BIN_DIR = bin
PROGRAMS = $(BIN_DIR)/pipe_benchmark $(BIN_DIR)/mq_benchmark $(BIN_DIR)/shm_benchmark

.PHONY: all clean
all: $(PROGRAMS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/pipe_benchmark: src/pipe_benchmark.c src/benchmark_common.h | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS_COMMON)

$(BIN_DIR)/mq_benchmark: src/mq_benchmark.c src/benchmark_common.h | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS_COMMON)

$(BIN_DIR)/shm_benchmark: src/shm_benchmark.c src/benchmark_common.h | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS_COMMON)

clean:
	rm -rf $(BIN_DIR)
