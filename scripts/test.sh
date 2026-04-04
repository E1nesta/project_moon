#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT_DIR/scripts/_common.sh"

build_local_binaries

ctest --test-dir "$(build_dir)" --output-on-failure "$@"
