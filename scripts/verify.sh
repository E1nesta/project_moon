#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "$0")" && pwd)/_common.sh"

"$ROOT_DIR/scripts/smoke.sh" "$@"
"$ROOT_DIR/scripts/run_gateway_fault_checks.sh"

echo "verify checks completed successfully"
