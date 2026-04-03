#!/usr/bin/env bash
set -euo pipefail

PREFIX="${GRPC_STACK_ROOT:-$HOME/.local/toolchains/grpc-stack}"
SRC_DIR="${GRPC_SRC_DIR:-$HOME/.local/src/grpc}"
BUILD_DIR="${GRPC_BUILD_DIR:-$SRC_DIR/cmake/build-grpc-stack}"
TAG="${GRPC_TAG:-v1.62.2}"

mkdir -p "$(dirname "$SRC_DIR")" "$PREFIX"

if [ ! -d "$SRC_DIR/.git" ]; then
  git clone --branch "$TAG" --depth 1 --recurse-submodules --shallow-submodules https://github.com/grpc/grpc "$SRC_DIR"
else
  git -C "$SRC_DIR" fetch --depth 1 origin "tag" "$TAG"
  git -C "$SRC_DIR" checkout -f "$TAG"
  git -C "$SRC_DIR" submodule sync --recursive
  git -C "$SRC_DIR" submodule update --init --recursive --depth 1
fi

cmake -S "$SRC_DIR" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DBUILD_TESTING=OFF \
  -DABSL_BUILD_TESTING=OFF \
  -DgRPC_INSTALL=ON \
  -DgRPC_BUILD_TESTS=OFF \
  -DgRPC_BUILD_GRPC_CPP_PLUGIN=ON \
  -DgRPC_BUILD_GRPC_CSHARP_PLUGIN=OFF \
  -DgRPC_BUILD_GRPC_NODE_PLUGIN=OFF \
  -DgRPC_BUILD_GRPC_OBJECTIVE_C_PLUGIN=OFF \
  -DgRPC_BUILD_GRPC_PHP_PLUGIN=OFF \
  -DgRPC_BUILD_GRPC_PYTHON_PLUGIN=OFF \
  -DgRPC_BUILD_GRPC_RUBY_PLUGIN=OFF \
  -DgRPC_ABSL_PROVIDER=module \
  -DgRPC_CARES_PROVIDER=module \
  -DgRPC_PROTOBUF_PROVIDER=module \
  -DgRPC_RE2_PROVIDER=module \
  -DgRPC_SSL_PROVIDER=module \
  -DgRPC_ZLIB_PROVIDER=module \
  -Dprotobuf_BUILD_TESTS=OFF \
  -DRE2_BUILD_TESTING=OFF

cmake --build "$BUILD_DIR" --target install -j"$(nproc)"
