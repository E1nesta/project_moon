#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "$0")" && pwd)/_common.sh"

compose_cmd up -d --wait

build_local_binaries
BUILD_DIR="$(build_dir)"

if [[ "$DEPLOY_PROFILE" == "delivery" ]]; then
  compose_cmd exec -T gateway_1 ./demo_client --config-profile delivery --happy-path-only
  exit 0
fi

ACCOUNT_MYSQL_HOST=127.0.0.1 \
ACCOUNT_MYSQL_PORT=3307 \
PLAYER_MYSQL_HOST=127.0.0.1 \
PLAYER_MYSQL_PORT=3307 \
BATTLE_MYSQL_HOST=127.0.0.1 \
BATTLE_MYSQL_PORT=3307 \
ACCOUNT_REDIS_HOST=127.0.0.1 \
ACCOUNT_REDIS_PORT=6379 \
PLAYER_REDIS_HOST=127.0.0.1 \
PLAYER_REDIS_PORT=6379 \
BATTLE_REDIS_HOST=127.0.0.1 \
BATTLE_REDIS_PORT=6379 \
"$BUILD_DIR/demo_flow" \
  --login-config configs/demo/login_server.conf \
  --player-config configs/demo/player_server.conf \
  --dungeon-config configs/demo/dungeon_server.conf \
  --happy-path-only

SKIP_BUILD=1 run_demo_client --no-reset --happy-path-only
