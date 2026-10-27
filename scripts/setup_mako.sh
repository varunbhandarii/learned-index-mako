#!/usr/bin/env bash
# Fetch the pinned Mako and ALEX sources and apply patches/mako.patch to Mako.
# Safe to run more than once.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if [ ! -f third_party/ALEX/src/core/alex.h ]; then
  git submodule update --init third_party/ALEX
fi
if [ ! -f third_party/mako/CMakeLists.txt ]; then
  # Mako's history is large; fetch only the pinned commit when the server allows it.
  git submodule update --init --depth 1 third_party/mako ||
    git submodule update --init third_party/mako
fi
git -C third_party/mako submodule update --init --recursive --depth 1

if git -C third_party/mako apply --reverse --check "$ROOT/patches/mako.patch" 2>/dev/null; then
  echo "patches/mako.patch is already applied"
else
  git -C third_party/mako apply "$ROOT/patches/mako.patch"
  echo "applied patches/mako.patch"
fi
