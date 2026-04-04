$ErrorActionPreference = "Stop"

cmake --preset dev-debug
cmake --build --preset dev-debug

./build/dev-debug/gateway_server.exe --config configs/gateway_server.conf --check
./build/dev-debug/login_server.exe --config configs/login_server.conf --check
./build/dev-debug/player_server.exe --config configs/player_server.conf --check
./build/dev-debug/player_internal_grpc_server.exe --config configs/player_internal_grpc_server.conf --check
./build/dev-debug/dungeon_server.exe --config configs/dungeon_server.conf --check
./build/dev-debug/demo_flow.exe --login-config configs/login_server.conf --player-config configs/player_server.conf --dungeon-config configs/dungeon_server.conf
