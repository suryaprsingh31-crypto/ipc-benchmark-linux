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

In Ubuntu/WSL, install missing tools with:

```bash
sudo apt update
sudo apt install build-essential make strace python3 python3-pip python3-matplotlib
```

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

The requested capacity is a message count. Shared memory allocates exactly that many slots. POSIX message queues use it as `mq_maxmsg` and use the message size as `mq_msgsize`. For pipes, the code requests `capacity × message_size` bytes with `F_SETPIPE_SZ`; Linux may round or reject that request due to kernel/user limits, so the result reports the actual pipe capacity (bytes) separately. The queue implementation also depends on the host's `/proc/sys/fs/mqueue/msg_max` and `msgsize_max`; choose supported capacities and sizes. Typical Ubuntu defaults allow at most 10 queued messages and a message size up to 8192 bytes. The benchmark fails with the system error if a requested queue cannot be created.

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

Outputs:

- `results/results.csv`: one row per timed repeat, retaining all samples rather than only averages.
- `results/strace/*.txt`: syscall-count summaries, one for each method, message count, and message size. Review calls, errors, and time percentages as well as syscall counts.
- `results/metadata.txt`: kernel, architecture, CPU (when exposed), compiler flags, workload, system load, and POSIX message-queue limits for reproducibility.

For a quick timing-only run without syscall summaries, add `--skip-strace`. Ensure each selected message size and queue capacity fits the host's POSIX message queue limits. Keep message count, message size, capacity, machine load, and kernel session constant across methods. Run on an otherwise idle machine and repeat the full experiment if results have high spread. Avoid comparing results collected with different WSL/host resource settings.

## Generate plots

```bash
python3 scripts/plot_results.py results/results.csv
```

This creates `results/ipc_comparison.png`, plotting median throughput and messages per second by message size; error bars show the interquartile range. Keep the raw CSV and `strace` summaries with the plot so the report can show run-to-run variation and syscall behavior.

## Benchmark interpretation notes

- `actual_capacity_bytes` makes kernel-granted pipe capacity visible; the requested capacity is not guaranteed for pipes.
- Small pipe messages can yield a larger effective pipe buffer than the requested message capacity because Linux rounds pipe capacity to page-size units. Compare this field when interpreting backpressure and throughput; POSIX queue limits can also constrain capacity.
- Message queues preserve message boundaries. Pipes are a byte stream, so the pipe consumer reads the fixed-size records by counting bytes.
- Shared-memory synchronization uses a bounded ring buffer with a process-shared mutex and two process-shared condition variables. This measures copying plus synchronization; it does not claim shared memory is lock-free.
- `strace -c` is useful for comparing syscall frequency and time, but its traced elapsed times must not be substituted for the untraced CSV timings.
- Report the metadata, compiler flags, workload parameters, and any queue-limit changes along with results. Do not change the system limits for only one method/configuration.
