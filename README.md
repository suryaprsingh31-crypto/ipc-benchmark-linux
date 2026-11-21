# IPC Benchmark: Pipes vs Message Queues vs Shared Memory

Operating Systems project: a Linux producer–consumer comparison of anonymous pipes, POSIX message queues, and POSIX shared memory with process-shared pthread synchronization.

## Requirements

- Linux (Ubuntu or WSL2) with GCC, GNU Make, `strace`, Python 3, and Matplotlib.
- POSIX message queues mounted and enabled (normally `/dev/mqueue` on Ubuntu); POSIX shared memory is normally exposed through `/dev/shm`.
- Build libraries: `-lrt -pthread` (on some modern glibc systems `-lrt` is no longer needed, but it is retained for compatibility).

## WSL2 setup (Ubuntu)

Run these commands in PowerShell if Ubuntu is not already installed:

```powershell
wsl --install -d Ubuntu
wsl --update
```

Open Ubuntu and enter the repository. A Windows checkout is accessible under `/mnt/c`; for repeated full benchmark runs, keep the working copy, binaries, and result files in the WSL Linux filesystem (for example, under `~/OS_project`) to avoid Windows-mounted filesystem overhead in the surrounding collection workflow.

```bash
cd /mnt/c/Users/ASUS/Desktop/python/OS_project
bash scripts/setup_wsl2.sh
```

The setup helper installs missing Ubuntu packages (`build-essential`, `make`, `strace`, `python3`, and `python3-matplotlib`), verifies `/dev/shm` and `/dev/mqueue`, prints the active POSIX message-queue limits, and performs a clean build. It requires `sudo` only if packages are missing. For a manual installation instead:

```bash
sudo apt update
sudo apt install build-essential make strace python3 python3-matplotlib
```

To confirm the WSL version from PowerShell, use `wsl --list --verbose`; Ubuntu should show version `2`. The benchmark metadata records Linux/kernel and available host details. WSL2 results characterize that WSL2 environment, not bare-metal Linux.

## Build

From the repository directory in Linux/WSL:

```bash
make
```

This creates `bin/pipe_benchmark`, `bin/mq_benchmark`, and `bin/shm_benchmark`.

## Run one benchmark

All three programs share the same named command-line interface:

```text
PROGRAM [--messages COUNT] [--message-size BYTES] [--capacity SLOTS]
```

The defaults are 100,000 messages, 64 bytes per message, and a capacity of 8 messages. `--messages` and `--message-size` define the common workload; `--capacity` controls the bounded buffering policy. All values must be positive integers. Example:

```bash
./bin/pipe_benchmark --messages 100000 --message-size 64 --capacity 8
./bin/mq_benchmark --messages 100000 --message-size 64 --capacity 8
./bin/shm_benchmark --messages 100000 --message-size 64 --capacity 8
```

Each prints the same CSV header and one result row in this field order:

```text
method,messages,message_size_bytes,requested_capacity_messages,actual_capacity_bytes,elapsed_seconds,throughput_mib_s,messages_per_second
```

The program waits until its consumer is ready before starting the monotonic-clock timer. Setup, allocation, queue creation, and process creation are outside the timed interval; the start handoff, synchronization, payload transfer, consumer validation, and consumer completion are included. The producer and consumer exchange fixed-size messages filled with `A`; consumers validate the received bytes. Each method uses one producer process and one consumer process.

The requested capacity is a message count. Shared memory allocates exactly that many slots. POSIX message queues request `mq_maxmsg = capacity` (maximum queued messages) and `mq_msgsize = message_size_bytes` (maximum bytes in each message); all messages use priority 0. After `mq_open`, the program queries the created attributes and fails if the granted depth or message size differs from the request. Linux queue creation also depends on `/proc/sys/fs/mqueue/msg_max`, `msgsize_max`, and available queue resources. Typical Ubuntu defaults allow at most 10 queued messages and a message size up to 8192 bytes; check the current host values before choosing the workload grid. `mq_open` reports an error if the requested attributes cannot be created. The program closes the queue descriptors and unlinks its uniquely named queue after each run.

For pipes, the code requests `capacity × message_size` bytes with `F_SETPIPE_SZ`; Linux may round or reject that request due to kernel/user limits, so the result reports the actual pipe capacity (bytes) separately. A pipe is a byte stream, so the implementation frames each record by reading and writing exactly `message_size_bytes` per message and validates all payload bytes.

The shared-memory method creates a unique POSIX shared-memory object with `shm_open`, sizes it with `ftruncate`, and maps it with `mmap`. The mapping contains a bounded ring with one slot per requested message capacity. A process-shared pthread mutex protects the ring indexes and count; process-shared `not_empty` and `not_full` condition variables provide blocking backpressure. After the consumer exits, the parent destroys the synchronization objects, unmaps and closes the object, then unlinks its name.

## Collect repeated measurements and syscall summaries

The runner passes these same named workload and capacity options to each executable. It performs one warmup per method and configuration, randomizes method order for each repeat, validates the executable's CSV header, writes raw timing samples to CSV, then runs separate `strace -f -c` measurements. It sweeps both message counts and message sizes. `strace` is kept out of the timed measurements because tracing changes performance.

```bash
python3 scripts/run_benchmarks.py \
  --messages 10000 50000 \
  --sizes 8 64 256 1024 4096 \
  --capacity 8 \
  --warmups 1 \
  --repeats 5 \
  --out results
```

The documented experiment matrix is 2 message counts (10,000 and 50,000) × 5 payload sizes (8, 64, 256, 1,024, and 4,096 bytes) × 3 methods × 5 measured repetitions, plus 1 warmup per method/configuration. This produces 150 untraced timing samples, followed by 30 separate traced runs using the same 10,000/50,000 counts, sizes, and capacity 8. Each configuration retains its own requested and actual capacity information. To use another matrix, pass the desired counts, sizes, capacity, warmup count, and repetition count explicitly and preserve those parameters in the report.

Outputs:

- `results/results.csv`: one row per timed repeat, retaining all samples rather than only averages.
- `results/strace/*.txt`: syscall-count summaries, one for each method, message count, and message size. Review calls, errors, and time percentages as well as syscall counts.
- `results/metadata.txt`: kernel, architecture, CPU (when exposed), compiler flags, workload, system load, and POSIX message-queue limits for reproducibility.

For a quick timing-only run without syscall summaries, add `--skip-strace`. Ensure each selected message size and queue capacity fits the host's POSIX message queue limits. Keep message count, message size, capacity, machine load, and kernel session constant across methods. Run on an otherwise idle machine and repeat the full experiment if results have high spread. Avoid comparing results collected with different WSL/host resource settings.

For a single matching syscall summary, run (outside the timing collection):

```bash
strace -f -c -o results/strace/pipe.txt \
  ./bin/pipe_benchmark --messages 10000 --message-size 64 --capacity 8
strace -f -c -o results/strace/message_queue.txt \
  ./bin/mq_benchmark --messages 10000 --message-size 64 --capacity 8
strace -f -c -o results/strace/shared_memory.txt \
  ./bin/shm_benchmark --messages 10000 --message-size 64 --capacity 8
```

## Generate plots

```bash
python3 scripts/plot_results.py results/results.csv
```

This creates `results/ipc_comparison.png`, plotting median throughput and messages per second by message size; error bars show the interquartile range. Keep the raw CSV and `strace` summaries with the plot so the report can show run-to-run variation and syscall behavior.

Interpret throughput and message rate together: small messages emphasize per-message handling and synchronization, while larger payloads increase bytes moved per operation and can shift the bottleneck toward copying and memory bandwidth. These are hypotheses to compare with the collected results, not guaranteed rankings. Discuss the observed medians and IQR, any overlap or high spread, `actual_capacity_bytes`, and the syscall call counts/errors/system time. `strace` system time is not the benchmark's end-to-end elapsed time; tracing perturbs execution, so use it only as separate mechanism evidence. State that results describe the tested Linux/WSL2 host and these concrete implementations.

The full run collected on 2026-11-21 is saved under `results/full_run_2026-11-21/` with its raw CSV, metadata, 30 syscall summaries, plot, and `INTERPRETATION.md`. In this run, median throughput generally rose as payload size increased. Pipe had the highest median at 8, 64, 1,024, and 4,096 bytes, while POSIX message queues led at 256 bytes; shared memory approached the pipe result at 4,096 bytes. This is a descriptive result from five repeats on one WSL2 host, not a universal ranking. At the same requested depth of 8 records, the pipe had a page-rounded 4 KiB capacity for 8–256-byte payloads, versus 64–2,048 bytes for the queue and shared-memory ring, so those small-message results are not equal-byte-capacity comparisons.

## Benchmark interpretation notes

- `actual_capacity_bytes` makes kernel-granted pipe capacity visible; the requested capacity is not guaranteed for pipes.
- Small pipe messages can yield a larger effective pipe buffer than the requested message capacity because Linux rounds pipe capacity to page-size units. Compare this field when interpreting backpressure and throughput; POSIX queue limits can also constrain capacity.
- Message queues preserve message boundaries. Pipes are a byte stream, so the pipe consumer reads the fixed-size records by counting bytes.
- Shared-memory synchronization uses a bounded ring buffer with a process-shared mutex and two process-shared condition variables. This measures copying plus synchronization; it does not claim shared memory is lock-free.
- `strace -c` is useful for comparing syscall frequency and time, but its traced elapsed times must not be substituted for the untraced CSV timings.
- Report the metadata, compiler flags, workload parameters, and any queue-limit changes along with results. Do not change the system limits for only one method/configuration.
