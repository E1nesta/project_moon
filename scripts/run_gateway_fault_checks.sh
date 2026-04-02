#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
COMPOSE_FILE="$ROOT_DIR/deploy/docker-compose.yml"
LOG_WINDOW="${DEMO_LOG_WINDOW:-10m}"

cd "$ROOT_DIR"

STOPPED_SERVICES=()

if [[ "${SKIP_BUILD:-0}" != "1" ]]; then
  cmake -S . -B build
  cmake --build build -j
fi

docker compose -f "$COMPOSE_FILE" up -d --build --force-recreate --wait

demo_client() {
  ./build/demo_client \
    --gateway-config configs/gateway.conf \
    --login-config configs/login_server.conf \
    --game-config configs/game_server.conf \
    --dungeon-config configs/dungeon_server.conf \
    "$@"
}

run_with_retry() {
  local attempts="$1"
  shift

  local try=1
  while (( try <= attempts )); do
    if "$@"; then
      return 0
    fi

    if (( try == attempts )); then
      return 1
    fi

    sleep 1
    ((try++))
  done
}

wait_service() {
  local service="$1"
  local container_id
  local state
  local health

  docker compose -f "$COMPOSE_FILE" start "$service" >/dev/null
  container_id="$(docker compose -f "$COMPOSE_FILE" ps -q "$service")"
  if [[ -z "$container_id" ]]; then
    echo "failed to resolve container id for $service" >&2
    return 1
  fi

  for _ in $(seq 1 60); do
    state="$(docker inspect --format '{{.State.Status}}' "$container_id")"
    health="$(docker inspect --format '{{if .State.Health}}{{.State.Health.Status}}{{else}}none{{end}}' "$container_id")"
    if [[ "$state" == "running" && ( "$health" == "healthy" || "$health" == "none" ) ]]; then
      return 0
    fi
    sleep 1
  done

  echo "service $service did not become healthy in time" >&2
  return 1
}

mark_stopped() {
  STOPPED_SERVICES+=("$1")
}

cleanup() {
  local service
  for (( idx=${#STOPPED_SERVICES[@]}-1; idx>=0; idx-- )); do
    service="${STOPPED_SERVICES[idx]}"
    wait_service "$service" || true
  done
}

trap cleanup EXIT

other_gateway() {
  if [[ "$1" == "gateway_1" ]]; then
    echo "gateway_2"
  else
    echo "gateway_1"
  fi
}

echo "[1/3] failover: stop gateway_1 and verify new connections reach gateway_2"
docker compose -f "$COMPOSE_FILE" stop gateway_1 >/dev/null
mark_stopped gateway_1
FAILOVER_MARK="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
run_with_retry 3 demo_client --happy-path-only --skip-session-recovery
if ! docker compose -f "$COMPOSE_FILE" logs gateway_2 --since "$FAILOVER_MARK" | grep -q "forwarding request to upstream"; then
  echo "failover check did not observe traffic on gateway_2 after gateway_1 was stopped" >&2
  exit 1
fi
wait_service gateway_1

echo "[2/3] upstream error: stop dungeon_server and expect SERVICE_UNAVAILABLE on enter"
docker compose -f "$COMPOSE_FILE" stop dungeon_server >/dev/null
mark_stopped dungeon_server
demo_client --happy-path-only --skip-session-recovery --expect-enter-error SERVICE_UNAVAILABLE
wait_service dungeon_server

echo "[3/3] session expiry: force cross-gateway recovery to fail with SESSION_INVALID"
LOGIN_MARK="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
LOGIN_OUTPUT="$(demo_client --scenario login-only)"
SESSION_ID="$(printf '%s\n' "$LOGIN_OUTPUT" | awk -F= '/^SESSION_ID=/{print $2}')"
ACCOUNT_ID="$(printf '%s\n' "$LOGIN_OUTPUT" | awk -F= '/^ACCOUNT_ID=/{print $2}')"
PLAYER_ID="$(printf '%s\n' "$LOGIN_OUTPUT" | awk -F= '/^PLAYER_ID=/{print $2}')"

if [[ -z "$SESSION_ID" || -z "$ACCOUNT_ID" || -z "$PLAYER_ID" ]]; then
  echo "failed to parse login-only output" >&2
  printf '%s\n' "$LOGIN_OUTPUT" >&2
  exit 1
fi

LOGIN_GATEWAY=""
for service in gateway_1 gateway_2; do
  if docker compose -f "$COMPOSE_FILE" logs "$service" --since "$LOGIN_MARK" | grep -q "$SESSION_ID"; then
    LOGIN_GATEWAY="$service"
    break
  fi
done

if [[ -z "$LOGIN_GATEWAY" ]]; then
  echo "failed to identify which gateway handled login for session $SESSION_ID" >&2
  exit 1
fi

RECOVERY_GATEWAY="$(other_gateway "$LOGIN_GATEWAY")"
docker compose -f "$COMPOSE_FILE" exec -T redis redis-cli DEL "session:$SESSION_ID" "account:session:$ACCOUNT_ID" >/dev/null
docker compose -f "$COMPOSE_FILE" stop "$LOGIN_GATEWAY" >/dev/null
mark_stopped "$LOGIN_GATEWAY"
RECOVERY_MARK="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
run_with_retry 3 demo_client --scenario load-only --session-id "$SESSION_ID" --player-id "$PLAYER_ID" --expect-load-error SESSION_INVALID
if ! docker compose -f "$COMPOSE_FILE" logs "$RECOVERY_GATEWAY" --since "$RECOVERY_MARK" | grep -q "session restore failed"; then
  echo "expected session restore failure log on $RECOVERY_GATEWAY" >&2
  exit 1
fi
wait_service "$LOGIN_GATEWAY"

echo "gateway fault checks completed successfully"
