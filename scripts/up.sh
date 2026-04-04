#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "$0")" && pwd)/_common.sh"

build_local_binaries
COMPOSE_BUILD="${COMPOSE_BUILD:-0}" up_stack
