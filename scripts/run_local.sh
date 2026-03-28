#!/usr/bin/env bash
set -euo pipefail

cmake -S . -B build
cmake --build build -j

./build/gateway --config configs/gateway.conf --check
./build/login_server --config configs/login_server.conf --check
./build/game_server --config configs/game_server.conf --check
./build/demo_flow --login-config configs/login_server.conf --game-config configs/game_server.conf
