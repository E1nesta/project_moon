#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEPLOY_PROFILE="${DEPLOY_PROFILE:-demo}"

configure_preset() {
  printf '%s\n' "${CMAKE_CONFIGURE_PRESET:-grpc-stack-debug}"
}

build_preset() {
  printf '%s\n' "${CMAKE_BUILD_PRESET:-$(configure_preset)}"
}

build_dir() {
  printf '%s\n' "$ROOT_DIR/build/$(configure_preset)"
}

export_grpc_stack_env() {
  local grpc_stack_root="${GRPC_STACK_ROOT:-$HOME/.local/toolchains/grpc-stack}"
  export GRPC_STACK_ROOT="$grpc_stack_root"
  export PATH="$grpc_stack_root/bin:$PATH"

  if [[ -n "${CMAKE_PREFIX_PATH:-}" ]]; then
    export CMAKE_PREFIX_PATH="$grpc_stack_root:$CMAKE_PREFIX_PATH"
  else
    export CMAKE_PREFIX_PATH="$grpc_stack_root"
  fi

  if [[ -n "${PKG_CONFIG_PATH:-}" ]]; then
    export PKG_CONFIG_PATH="$grpc_stack_root/lib/pkgconfig:$PKG_CONFIG_PATH"
  else
    export PKG_CONFIG_PATH="$grpc_stack_root/lib/pkgconfig"
  fi
}

ensure_grpc_stack() {
  export_grpc_stack_env
  if [[ -x "$GRPC_STACK_ROOT/bin/protoc" && -x "$GRPC_STACK_ROOT/bin/grpc_cpp_plugin" ]]; then
    return 0
  fi

  if pgrep -f "$ROOT_DIR/scripts/setup_grpc_stack.sh" >/dev/null 2>&1; then
    echo "waiting for existing gRPC stack build to finish..."
    while pgrep -f "$ROOT_DIR/scripts/setup_grpc_stack.sh" >/dev/null 2>&1; do
      sleep 5
    done
    if [[ -x "$GRPC_STACK_ROOT/bin/protoc" && -x "$GRPC_STACK_ROOT/bin/grpc_cpp_plugin" ]]; then
      return 0
    fi
  fi

  "$ROOT_DIR/scripts/setup_grpc_stack.sh"
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

  ensure_grpc_stack
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
