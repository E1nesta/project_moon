#!/usr/bin/env bash
set -euo pipefail

cmake -S . -B build
cmake --build build -j

./build/gateway_server --config configs/gateway_server.conf --check
./build/login_server --config configs/login_server.conf --check
./build/player_server --config configs/player_server.conf --check
./build/dungeon_server --config configs/dungeon_server.conf --check
./build/demo_flow \
  --login-config configs/login_server.conf \
  --player-config configs/player_server.conf \
  --dungeon-config configs/dungeon_server.conf

SKIP_BUILD=1 ./scripts/run_network_demo.sh
