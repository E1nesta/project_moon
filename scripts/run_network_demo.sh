#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

docker compose -f deploy/docker-compose.yml up -d

if [[ "${SKIP_BUILD:-0}" != "1" ]]; then
  cmake -S . -B build
  cmake --build build -j
fi

cleanup() {
  for pid in "${PIDS[@]:-}"; do
    if kill -0 "$pid" >/dev/null 2>&1; then
      kill "$pid" >/dev/null 2>&1 || true
      wait "$pid" 2>/dev/null || true
    fi
  done
}

trap cleanup EXIT

PIDS=()

./build/login_server --config configs/login_server.conf > build/login_server.log 2>&1 &
PIDS+=($!)
./build/game_server --config configs/game_server.conf > build/game_server.log 2>&1 &
PIDS+=($!)
./build/dungeon_server --config configs/dungeon_server.conf > build/dungeon_server.log 2>&1 &
PIDS+=($!)
./build/gateway --config configs/gateway.conf > build/gateway.log 2>&1 &
PIDS+=($!)

sleep 2

./build/demo_client \
  --gateway-config configs/gateway.conf \
  --login-config configs/login_server.conf \
  --game-config configs/game_server.conf \
  --dungeon-config configs/dungeon_server.conf
