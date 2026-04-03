$ErrorActionPreference = "Stop"

$env:GRPC_STACK_ROOT = if ($env:GRPC_STACK_ROOT) { $env:GRPC_STACK_ROOT } else { "$HOME/.local/toolchains/grpc-stack" }
$env:PATH = "$env:GRPC_STACK_ROOT/bin;$env:PATH"
$env:CMAKE_PREFIX_PATH = if ($env:CMAKE_PREFIX_PATH) { "$env:GRPC_STACK_ROOT;$env:CMAKE_PREFIX_PATH" } else { "$env:GRPC_STACK_ROOT" }
$env:PKG_CONFIG_PATH = if ($env:PKG_CONFIG_PATH) { "$env:GRPC_STACK_ROOT/lib/pkgconfig;$env:PKG_CONFIG_PATH" } else { "$env:GRPC_STACK_ROOT/lib/pkgconfig" }

cmake --preset grpc-stack-debug
cmake --build --preset grpc-stack-debug

./build/grpc-stack-debug/gateway_server.exe --config configs/gateway_server.conf --check
./build/grpc-stack-debug/login_server.exe --config configs/login_server.conf --check
./build/grpc-stack-debug/player_server.exe --config configs/player_server.conf --check
./build/grpc-stack-debug/player_internal_grpc_server.exe --config configs/player_internal_grpc_server.conf --check
./build/grpc-stack-debug/dungeon_server.exe --config configs/dungeon_server.conf --check
./build/grpc-stack-debug/demo_flow.exe --login-config configs/login_server.conf --player-config configs/player_server.conf --dungeon-config configs/dungeon_server.conf
