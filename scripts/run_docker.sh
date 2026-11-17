#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

export LOCAL_UID="$(id -u)"
export LOCAL_GID="$(id -g)"

if command -v xhost >/dev/null 2>&1 && [[ -n "${DISPLAY:-}" ]]; then
    xhost +local:docker >/dev/null 2>&1 || true
fi

docker compose up --build agrovis-app
