$ErrorActionPreference = "Stop"

cmake -S . -B build
cmake --build build --config Debug

./build/Debug/gateway.exe --config configs/gateway.conf --check
./build/Debug/login_server.exe --config configs/login_server.conf --check
./build/Debug/game_server.exe --config configs/game_server.conf --check
./build/Debug/demo_flow.exe --login-config configs/login_server.conf --game-config configs/game_server.conf
