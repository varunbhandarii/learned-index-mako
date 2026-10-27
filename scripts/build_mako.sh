#!/usr/bin/env bash
# Build Mako's TPC-C driver (dbtest) and smoke test with the ALEX index linked in.
# Usage: scripts/build_mako.sh [jobs]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
JOBS="${1:-$(nproc)}"

"$ROOT/scripts/setup_mako.sh"
cd "$ROOT/third_party/mako"
cmake -S . -B build
cmake --build build --target dbtest simpleTransaction --parallel "$JOBS"
echo "built $ROOT/third_party/mako/build/dbtest"
