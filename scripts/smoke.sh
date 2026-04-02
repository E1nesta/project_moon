#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "$0")" && pwd)/_common.sh"

LOG_WINDOW="${DEMO_LOG_WINDOW:-10m}"

wait_for_log_pattern() {
  local pattern="$1"
  shift

  local attempt
  for attempt in $(seq 1 10); do
    if compose_cmd logs "$@" --since "$LOG_WINDOW" | grep -q "$pattern"; then
      return 0
    fi
    sleep 1
  done

  return 1
}

build_local_binaries
COMPOSE_BUILD="${COMPOSE_BUILD:-0}" up_stack

run_demo_client --happy-path-only "$@"

for service in gateway_1 gateway_2; do
  if ! wait_for_log_pattern "forwarding request to upstream" "$service"; then
    echo "expected traffic in $service logs, but no forwarded request was observed" >&2
    exit 1
  fi
done

if ! wait_for_log_pattern "bind restored from redis" gateway_1 gateway_2; then
  echo "expected at least one redis-backed binding restore in gateway logs" >&2
  exit 1
fi

echo "smoke checks completed successfully"
