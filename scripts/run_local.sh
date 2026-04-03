#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "$0")" && pwd)/_common.sh"

build_local_binaries
BUILD_DIR="$(build_dir)"

"$BUILD_DIR/gateway_server" --config configs/gateway_server.conf --check
"$BUILD_DIR/login_server" --config configs/login_server.conf --check
"$BUILD_DIR/player_server" --config configs/player_server.conf --check
"$BUILD_DIR/player_internal_grpc_server" --config configs/player_internal_grpc_server.conf --check
"$BUILD_DIR/dungeon_server" --config configs/dungeon_server.conf --check
"$BUILD_DIR/demo_flow" \
  --login-config configs/login_server.conf \
  --player-config configs/player_server.conf \
  --dungeon-config configs/dungeon_server.conf

SKIP_BUILD=1 ./scripts/run_network_demo.sh
