#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEPLOY_PROFILE="${DEPLOY_PROFILE:-demo}"

configure_preset() {
  printf '%s\n' "${CMAKE_CONFIGURE_PRESET:-dev-debug}"
}

build_preset() {
  printf '%s\n' "${CMAKE_BUILD_PRESET:-$(configure_preset)}"
}

build_dir() {
  printf '%s\n' "$ROOT_DIR/build/$(configure_preset)"
}

has_system_grpc_toolchain() {
  command -v protoc >/dev/null 2>&1 &&
  command -v grpc_cpp_plugin >/dev/null 2>&1
}

prepare_grpc_toolchain() {
  if has_system_grpc_toolchain; then
    return 0
  fi

  cat <<'EOF' >&2
missing gRPC/Protobuf toolchain

Install system packages such as:
  protobuf-compiler libprotobuf-dev libgrpc++-dev protobuf-compiler-grpc
EOF
  return 1
}

compose_file() {
  if [[ "$DEPLOY_PROFILE" == "delivery" ]]; then
    printf '%s\n' "$ROOT_DIR/deploy/docker-compose.delivery.yml"
  else
    printf '%s\n' "$ROOT_DIR/deploy/docker-compose.yml"
  fi
}

compose_env_file() {
  if [[ "$DEPLOY_PROFILE" == "delivery" ]]; then
    if [[ -f "$ROOT_DIR/deploy/.env.delivery" ]]; then
      printf '%s\n' "$ROOT_DIR/deploy/.env.delivery"
    else
      printf '%s\n' "$ROOT_DIR/deploy/.env.delivery.example"
    fi
  else
    printf '%s\n' "$ROOT_DIR/deploy/.env.demo"
  fi
}

compose_cmd() {
  docker compose --env-file "$(compose_env_file)" -f "$(compose_file)" "$@"
}

build_local_binaries() {
  if [[ "${SKIP_BUILD:-0}" == "1" ]]; then
    return 0
  fi

  prepare_grpc_toolchain
  (
    cd "$ROOT_DIR"
    cmake --preset "$(configure_preset)"
    cmake --build --preset "$(build_preset)" --parallel
  )
}

up_stack() {
  local build_flag=()
  if [[ "${COMPOSE_BUILD:-0}" == "1" ]]; then
    build_flag=(--build)
  fi

  compose_cmd up -d "${build_flag[@]}" --wait
}

run_demo_client() {
  if [[ "$DEPLOY_PROFILE" == "delivery" ]]; then
    compose_cmd exec -T gateway_1 ./demo_client --config-profile delivery "$@"
  else
    MYSQL_HOST=127.0.0.1 \
    MYSQL_PORT=3307 \
    REDIS_HOST=127.0.0.1 \
    REDIS_PORT=6379 \
    "$(build_dir)/demo_client" --config-profile demo "$@"
  fi
}
