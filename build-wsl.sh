#!/usr/bin/env bash
# Root-relative configure + build for the vixen-wsl preset. Run from any directory.
#
# The preset file lives in VIXEN/ (its sourceDir), but its binaryDir is <repo>/build/wsl.
# This wrapper supplies both halves so callers never guess the cwd or the ../build path.
#
# Usage: ./build-wsl.sh [-DVAR=VALUE ...] [Release] [cmake --build args]
#   ./build-wsl.sh --target VIXEN -j4
#   ./build-wsl.sh -DVIXEN_WITH_FSR3=OFF --target VIXEN -j4   (-D/-U go to configure only)
#   ./build-wsl.sh --clean                                    (delete only <repo>/build/wsl)
# A bare "Release" is accepted because the preset already selects it; any other build
# type is rejected here instead of failing later inside `cmake --build`.
# BUILD_WSL_DRY_RUN=1 prints the cmake commands instead of running them.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
tree="$root/build/wsl"

configure_args=()
build_args=()
while (($#)); do
    case "$1" in
        --clean)
            if (($# > 1)); then
                echo "build-wsl.sh: --clean takes no other arguments (got: ${*:2}). Fix: run './build-wsl.sh --clean' alone, then rebuild." >&2
                exit 2
            fi
            echo "[build-wsl] removing $tree"
            [[ -d "$tree" ]] && find "$tree" -depth -delete
            exit 0 ;;
        -D|-U)
            (($# > 1)) || { echo "build-wsl.sh: $1 needs a value, e.g. $1 VIXEN_WITH_FSR3=OFF." >&2; exit 2; }
            configure_args+=("$1" "$2"); shift 2 ;;
        -D?*|-U?*) configure_args+=("$1"); shift ;;
        Release) shift ;;
        Debug|RelWithDebInfo|MinSizeRel)
            echo "build-wsl.sh: build type '$1' is not available through the vixen-wsl preset, which is fixed to Release (binary tree $tree)." >&2
            echo "Fix: use the vixen-wsl-debug preset directly (cmake -S VIXEN --preset vixen-wsl-debug), or pass -DCMAKE_BUILD_TYPE=$1 to a separate tree." >&2
            exit 2 ;;
        *) build_args+=("$1"); shift ;;
    esac
done

run() {
    if [[ -n "${BUILD_WSL_DRY_RUN:-}" ]]; then printf '%s\n' "$*"; else "$@"; fi
}

run cmake -S "$root/VIXEN" --preset vixen-wsl "${configure_args[@]}"
echo "[build-wsl] configure ok: $tree"
run cmake --build "$tree" "${build_args[@]}"
echo "[build-wsl] build ok: $tree (${build_args[*]:-all targets})"
