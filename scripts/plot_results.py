#!/usr/bin/env python3
"""Plot median IPC performance with interquartile-range error bars."""
import argparse
import csv
import statistics
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt

LABELS = {"pipe": "Pipe", "message_queue": "POSIX message queue", "shared_memory": "POSIX shared memory"}
COLORS = {"pipe": "#2563eb", "message_queue": "#ea580c", "shared_memory": "#15803d"}


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("csv", nargs="?", type=Path, default=Path("results/results.csv"))
    ap.add_argument("--output", type=Path, default=Path("results/ipc_comparison.png"))
    args = ap.parse_args()
    groups = defaultdict(list)
    with args.csv.open(newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            groups[(row["method"], int(row["messages"]), int(row["message_size_bytes"]))].append(row)
    if not groups:
        raise SystemExit(f"No samples found in {args.csv}")
    counts = sorted({key[1] for key in groups})
    sizes = sorted({key[2] for key in groups})
    fig, axes = plt.subplots(len(counts), 2, figsize=(12, 5 * len(counts)), squeeze=False, constrained_layout=True)
    for row_index, count in enumerate(counts):
        for method in LABELS:
            medians, lows, highs, msg_rates = [], [], [], []
            for size in sizes:
                rows = groups.get((method, count, size), [])
                values = sorted(float(r["throughput_mib_s"]) for r in rows)
                if not values:
                    medians.append(float("nan")); lows.append(0); highs.append(0); msg_rates.append(float("nan")); continue
                median = statistics.median(values)
                if len(values) == 1:
                    q1 = q3 = median
                else:
                    q1, _, q3 = statistics.quantiles(values, n=4, method="inclusive")
                medians.append(median); lows.append(max(0.0, median - q1)); highs.append(max(0.0, q3 - median))
                msg_rates.append(statistics.median(float(r["messages_per_second"]) for r in rows))
            axes[row_index, 0].errorbar(sizes, medians, yerr=[lows, highs], marker="o", capsize=3,
                                        label=LABELS[method], color=COLORS[method])
            axes[row_index, 1].plot(sizes, msg_rates, marker="o", label=LABELS[method], color=COLORS[method])
        axes[row_index, 0].set_title(f"{count:,} messages — throughput")
        axes[row_index, 1].set_title(f"{count:,} messages — message rate")
        axes[row_index, 0].set_ylabel("Throughput (MiB/s)")
        axes[row_index, 1].set_ylabel("Messages per second")
        for ax in axes[row_index]:
            ax.set_xlabel("Message size (bytes)")
            ax.set_xscale("log", base=2)
            ax.grid(True, alpha=0.3)
            ax.legend()
    fig.suptitle("Producer–consumer IPC benchmark (median; bars show IQR)")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.output, dpi=180)
    print(f"Saved {args.output}")


if __name__ == "__main__":
    main()
