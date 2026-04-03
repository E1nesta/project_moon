#!/usr/bin/env bash
set -euo pipefail

docker compose -f deploy/docker-compose.yml up -d

cmake -S . -B build
cmake --build build -j

./build/demo_flow \
  --login-config configs/login_server.conf \
  --player-config configs/player_server.conf \
  --dungeon-config configs/dungeon_server.conf

SKIP_BUILD=1 ./scripts/run_network_demo.sh
