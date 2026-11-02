#!/usr/bin/env bash
# Root-relative configure + build for the vixen-wsl preset. Run from any directory.
#
# The preset file lives in VIXEN/ (its sourceDir), but its binaryDir is <repo>/build/wsl.
# This wrapper supplies both halves so callers never guess the cwd or the ../build path.
#
# Usage: ./build-wsl.sh [cmake --build args]   e.g. ./build-wsl.sh --target VIXEN -j4
set -euo pipefail

# cmake --build takes options only (--config <cfg>, --target <tgt>, -j [n]); a bare word such as
# `Release all` fails only after the full configure, so reject it before cmake -S runs.
if (($# > 0)) && [[ "$1" != -* ]]; then
    echo "build-wsl.sh: unexpected positional argument '$1'; use --config <cfg> --target <tgt>" >&2
    exit 2
fi

root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cmake -S "$root/VIXEN" --preset vixen-wsl
cmake --build "$root/build/wsl" "$@"
