# IPC Benchmark Results: Short Interpretation

## Run context

- Host: AMD Ryzen 7 7435HS; Ubuntu 24.04.1 on WSL2; kernel `6.6.87.2-microsoft-standard-WSL2`; x86_64.
- Toolchain: GCC 13.3.0, `-O2 -std=c11 -Wall -Wextra -Wpedantic`; Python 3.12.3 and Matplotlib 3.6.3.
- Workload: one producer and one consumer process; requested capacity 8; 10,000 and 50,000 messages; payloads 8, 64, 256, 1,024, and 4,096 bytes; one warmup and five measured repeats per method/configuration.
- Evidence: 150 untraced timing rows in `results.csv`, plus 30 separate `strace -f -c` summaries in `strace/`. The plot is `ipc_comparison.png`.
- WSL load average recorded at collection start: 0.43. Results are specific to this host/session and are not bare-metal Linux measurements.

## Observed throughput

Median throughput generally rose with payload size for all three implementations. For the 50,000-message workload, the medians and interquartile ranges (IQR) were:

| Payload (bytes) | Pipe MiB/s, median [IQR] | POSIX queue MiB/s, median [IQR] | POSIX shared memory MiB/s, median [IQR] |
|---:|---:|---:|---:|
| 8 | 7.553 [7.051, 7.572] | 4.197 [3.809, 4.252] | 0.897 [0.889, 0.952] |
| 64 | 42.755 [41.983, 43.028] | 37.142 [36.432, 38.874] | 7.333 [7.258, 7.368] |
| 256 | 129.424 [127.152, 137.326] | 155.529 [144.921, 158.982] | 30.405 [29.737, 31.837] |
| 1,024 | 576.785 [550.554, 587.735] | 404.523 [393.292, 413.588] | 193.821 [184.639, 207.734] |
| 4,096 | 1,137.993 [1,137.351, 1,157.497] | 768.391 [767.297, 779.660] | 1,012.501 [993.569, 1,029.326] |

Pipe had the highest median at 8, 64, 1,024, and 4,096 bytes; the POSIX queue led at 256 bytes. Shared memory had the lowest medians through 1,024 bytes, then approached pipe throughput at 4,096 bytes. The results do not support a single method being best at every payload size. The 10,000-message panels show broadly the same size-related pattern.

Message rate behaved differently from byte throughput: pipe and queue delivered more messages per second for smaller records, while each record carries fewer bytes. At 4,096 bytes, the shared-memory implementation’s median message rate rose relative to its small-payload rate, alongside its increased byte throughput. These values describe this ring-buffer implementation, which performs a copy into and out of shared slots and coordinates through a process-shared mutex and condition variables.

## Capacity comparability caveat

All methods requested a depth of 8 messages, but the effective byte capacities differed for small payloads:

| Payload (bytes) | Pipe actual capacity (bytes) | Queue and ring capacity (bytes) |
|---:|---:|---:|
| 8 | 4,096 | 64 |
| 64 | 4,096 | 512 |
| 256 | 4,096 | 2,048 |
| 1,024 | 8,192 | 8,192 |
| 4,096 | 32,768 | 32,768 |

Linux granted the pipe a page-rounded byte buffer, while the queue and ring used exactly eight fixed-size records. This gives the pipe more buffering for 8–256-byte payloads and may change producer/consumer backpressure. Interpret those rows as a comparison at equal requested record depth, not equal byte capacity. At 1,024 and 4,096 bytes the reported capacities match.

## Syscall evidence

For one representative traced configuration (10,000 messages × 64 bytes), `strace -f -c` reported 10,003 reads and 10,003 writes for pipe (including the readiness/start-gate pipe operations); 10,000 `mq_timedsend` plus 10,000 `mq_timedreceive` calls for POSIX queues; and 8,288 `futex` calls for shared memory, alongside `wait4` and setup/cleanup calls. The shared-memory summary marked 2,268 futex calls as errors; `-c` alone does not show their errno, so they should not be labeled application failures without a detailed trace. Traced syscall time is not the untraced end-to-end wall time and must not be compared as throughput.

## Limits and next refinements

Five repeats per configuration provide a useful initial spread estimate, not a strong statistical claim. IQR bars show the observed central spread and are not confidence intervals. Repeat the experiment in independent sessions if the report needs uncertainty intervals or stronger claims. The experiment uses one capacity policy, one producer/consumer pair, one constant payload pattern, and one WSL2 host. Future runs should test more capacity values and collect the host's CPU scheduling/load context; do not generalize this result to other kernels or IPC implementations.
