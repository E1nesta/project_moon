#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
COMPOSE_FILE="$ROOT_DIR/deploy/docker-compose.yml"
LOG_WINDOW="${DEMO_LOG_WINDOW:-10m}"

cd "$ROOT_DIR"

if [[ "${SKIP_BUILD:-0}" != "1" ]]; then
  cmake -S . -B build
  cmake --build build -j
fi

docker compose -f "$COMPOSE_FILE" up -d --build --force-recreate --wait

./build/demo_client \
  --gateway-config configs/gateway.conf \
  --login-config configs/login_server.conf \
  --game-config configs/game_server.conf \
  --dungeon-config configs/dungeon_server.conf \
  "$@"

for service in gateway_1 gateway_2; do
  if ! docker compose -f "$COMPOSE_FILE" logs "$service" --since "$LOG_WINDOW" | grep -q "forwarding request to upstream"; then
    echo "expected traffic in $service logs, but no forwarded request was observed" >&2
    exit 1
  fi
done

if ! docker compose -f "$COMPOSE_FILE" logs gateway_1 gateway_2 --since "$LOG_WINDOW" | grep -q "bind restored from redis"; then
  echo "expected at least one redis-backed binding restore in gateway logs" >&2
  exit 1
fi

echo "multi-gateway demo completed successfully"
