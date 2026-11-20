#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ ! -r /etc/os-release ]]; then
    echo "Cannot identify Linux distribution." >&2
    exit 1
fi
# shellcheck disable=SC1091
source /etc/os-release
if [[ "${ID:-}" != "ubuntu" && "${ID_LIKE:-}" != *debian* ]]; then
    echo "This setup helper supports Ubuntu/Debian on WSL2; detected ${PRETTY_NAME:-unknown}." >&2
    exit 1
fi

missing=()
for command_name in gcc make strace python3; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        missing+=("$command_name")
    fi
done
if ! python3 -c 'import matplotlib' >/dev/null 2>&1; then
    missing+=("python3-matplotlib")
fi

if ((${#missing[@]})); then
    echo "Installing required build and analysis packages..."
    sudo apt-get update
    sudo apt-get install -y build-essential make strace python3 python3-matplotlib
fi

for mount_path in /dev/shm /dev/mqueue; do
    if ! mountpoint -q "$mount_path"; then
        echo "Required IPC filesystem is not mounted: $mount_path" >&2
        echo "Check WSL2 systemd/mount configuration before running benchmarks." >&2
        exit 1
    fi
done

echo "Distribution: ${PRETTY_NAME:-unknown}"
gcc --version | head -n 1
make --version | head -n 1
strace -V 2>&1 | head -n 1
python3 --version
python3 -c 'import matplotlib; print("Matplotlib", matplotlib.__version__)'
echo "POSIX message queue limits: msg_max=$(cat /proc/sys/fs/mqueue/msg_max), msgsize_max=$(cat /proc/sys/fs/mqueue/msgsize_max)"

cd "$repo_dir"
make clean
make
echo "WSL2 build setup is ready in $repo_dir"
