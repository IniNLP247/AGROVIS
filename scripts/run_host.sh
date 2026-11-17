#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

export LD_LIBRARY_PATH="/home/inigo/Qt/6.11.1/gcc_64/lib:${LD_LIBRARY_PATH:-}"
cmake --preset linux-qt611
cmake --build --preset linux-qt611 -j"$(nproc)"
exec ./build/linux-qt611/app/agrovis_app "$@"
