#!/usr/bin/env python3
"""Collect comparable IPC timings and separate strace syscall summaries."""
import argparse
import csv
import datetime
import platform
import random
import re
import subprocess
import sys
from pathlib import Path

METHODS = ("pipe", "message_queue", "shared_memory")
COLUMNS = ("method", "messages", "message_size_bytes", "requested_capacity_messages",
           "actual_capacity_bytes", "elapsed_seconds", "throughput_mib_s", "messages_per_second")


def execute(binary: Path, messages: int, size: int, capacity: int) -> dict:
    command = [str(binary), str(messages), str(size), str(capacity)]
    proc = subprocess.run(command, check=False, text=True, capture_output=True)
    if proc.returncode:
        raise subprocess.CalledProcessError(proc.returncode, command, output=proc.stdout, stderr=proc.stderr)
    rows = list(csv.reader(proc.stdout.splitlines()))
    if len(rows) != 1 or len(rows[0]) != len(COLUMNS):
        raise RuntimeError(f"Unexpected benchmark output from {' '.join(command)}: {proc.stdout!r}")
    return dict(zip(COLUMNS, rows[0]))


def write_metadata(path: Path, args: argparse.Namespace) -> None:
    gcc = subprocess.run(["gcc", "--version"], check=True, text=True, capture_output=True).stdout.splitlines()[0]
    details = [
        f"collected_utc={datetime.datetime.now(datetime.timezone.utc).isoformat()}",
        f"platform={platform.platform()}",
        f"kernel={platform.release()}",
        f"architecture={platform.machine()}",
        f"python={platform.python_version()}",
        f"gcc={gcc}",
        "compiler_flags=-O2 -std=c11 -Wall -Wextra -Wpedantic",
        f"message_counts={','.join(map(str, args.messages))}",
        f"sizes_bytes={','.join(map(str, args.sizes))}",
        f"requested_capacity_messages={args.capacity}",
        f"warmups={args.warmups}",
        f"repeats={args.repeats}",
    ]
    cpuinfo = Path("/proc/cpuinfo")
    if cpuinfo.exists():
        for line in cpuinfo.read_text(errors="replace").splitlines():
            if line.lower().startswith("model name"):
                details.append(f"cpu={line.split(':', 1)[1].strip()}")
                break
    loadavg = Path("/proc/loadavg")
    if loadavg.exists():
        details.append(f"load_average_at_start={loadavg.read_text().split()[0]}")
    for key in ("msg_max", "msgsize_max", "queues_max"):
        setting = Path("/proc/sys/fs/mqueue") / key
        if setting.exists():
            details.append(f"mqueue_{key}={setting.read_text().strip()}")
    (path / "metadata.txt").write_text("\n".join(details) + "\n", encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--messages", type=int, nargs="+", default=[10000, 50000],
                    help="one or more producer message counts")
    ap.add_argument("--sizes", type=int, nargs="+", default=[8, 64, 256, 1024, 4096])
    ap.add_argument("--capacity", type=int, default=8)
    ap.add_argument("--repeats", type=int, default=5)
    ap.add_argument("--warmups", type=int, default=1)
    ap.add_argument("--out", type=Path, default=Path("results"))
    ap.add_argument("--skip-strace", action="store_true", help="skip separate syscall-summary runs")
    args = ap.parse_args()
    if any(m <= 0 for m in args.messages) or args.capacity <= 0 or args.repeats <= 0 or args.warmups < 0 or any(s <= 0 for s in args.sizes):
        ap.error("messages, sizes, capacity, and repeats must be positive; warmups cannot be negative")
    root = Path(__file__).resolve().parents[1]
    binary_names = {"pipe": "pipe_benchmark", "message_queue": "mq_benchmark", "shared_memory": "shm_benchmark"}
    binaries = {name: root / "bin" / f"{binary_names[name]}" for name in METHODS}
    missing = [str(p) for p in binaries.values() if not p.exists()]
    if missing:
        raise SystemExit("Missing binaries; run `make` first:\n" + "\n".join(missing))
    args.out.mkdir(parents=True, exist_ok=True)
    write_metadata(args.out, args)
    samples = []
    rng = random.Random(2026)
    total = len(args.sizes) * len(args.messages) * (args.warmups + args.repeats) * len(METHODS)
    done = 0
    for size in args.sizes:
        for messages in args.messages:
            for trial in range(-args.warmups, args.repeats):
                order = list(METHODS)
                rng.shuffle(order)
                for method in order:
                    result = execute(binaries[method], messages, size, args.capacity)
                    done += 1
                    run_label = f"warmup {trial + args.warmups + 1}/{args.warmups}" if trial < 0 else f"run {trial + 1}/{args.repeats}"
                    print(f"[{done}/{total}] {method}, messages={messages}, size={size}, {run_label}", flush=True)
                    if trial >= 0:
                        result["trial"] = trial + 1
                        samples.append(result)
    result_file = args.out / "results.csv"
    with result_file.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=[*COLUMNS, "trial"])
        writer.writeheader()
        writer.writerows(samples)
    if not args.skip_strace:
        trace_dir = args.out / "strace"
        trace_dir.mkdir(exist_ok=True)
        generated_name = re.compile(r"^(pipe|message_queue|shared_memory)_messages-\d+_size-\d+_capacity-\d+\.txt$")
        for old_file in trace_dir.iterdir():
            if old_file.is_file() and generated_name.fullmatch(old_file.name):
                old_file.unlink()
        for size in args.sizes:
            for messages in args.messages:
                for method in METHODS:
                    stem = f"{method}_messages-{messages}_size-{size}_capacity-{args.capacity}"
                    trace_path = trace_dir / f"{stem}.txt"
                    command = ["strace", "-f", "-c", "-o", str(trace_path), str(binaries[method]),
                               str(messages), str(size), str(args.capacity)]
                    subprocess.run(command, check=True, stdout=subprocess.DEVNULL)
                    print(f"Saved syscall summary: {trace_path}")
    print(f"Saved timing samples: {result_file}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as exc:
        print(f"Command failed ({exc.returncode}): {exc.cmd}", file=sys.stderr)
        if exc.stderr:
            print(exc.stderr, file=sys.stderr, end="" if exc.stderr.endswith("\n") else "\n")
        raise SystemExit(exc.returncode)
