#!/usr/bin/env bash
# Root-relative configure + build for the vixen-wsl preset. Run from any directory.
#
# The preset file lives in VIXEN/ (its sourceDir), but its binaryDir is <repo>/build/wsl.
# This wrapper supplies both halves so callers never guess the cwd or the ../build path.
#
# Usage: ./build-wsl.sh [cmake --build args]   e.g. ./build-wsl.sh --target VIXEN -j4
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cmake -S "$root/VIXEN" --preset vixen-wsl
cmake --build "$root/build/wsl" "$@"
