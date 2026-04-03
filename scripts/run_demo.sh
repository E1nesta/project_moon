#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "$0")" && pwd)/_common.sh"

compose_cmd up -d --wait

build_local_binaries
BUILD_DIR="$(build_dir)"

"$BUILD_DIR/demo_flow" \
  --login-config configs/login_server.conf \
  --player-config configs/player_server.conf \
  --dungeon-config configs/dungeon_server.conf

SKIP_BUILD=1 ./scripts/run_network_demo.sh
