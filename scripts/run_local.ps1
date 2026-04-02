$ErrorActionPreference = "Stop"

cmake -S . -B build
cmake --build build --config Debug

./build/Debug/gateway_server.exe --config configs/gateway_server.conf --check
./build/Debug/login_server.exe --config configs/login_server.conf --check
./build/Debug/player_server.exe --config configs/player_server.conf --check
./build/Debug/dungeon_server.exe --config configs/dungeon_server.conf --check
./build/Debug/demo_flow.exe --login-config configs/login_server.conf --player-config configs/player_server.conf --dungeon-config configs/dungeon_server.conf
