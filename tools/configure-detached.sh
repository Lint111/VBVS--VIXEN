#!/usr/bin/env bash
# Configure a historical VIXEN commit in its own detached worktree (matched regression captures).
#
# Usage: tools/configure-detached.sh [options] <sha>
#   --kernel-sha <sha>   set VIXEN_YEROKET_KERNEL_SHA_OVERRIDE (default: the commit's tracked pin)
#   --worktree <dir>     detached worktree path (default: <repo>/.tmp/detached/<short sha>)
#   --build-dir <dir>    build tree (default: <worktree>/build/wsl)
#   --dry-run            print the resolved inputs and the cmake command, run nothing
# Environment overrides: UNDERTOW_ROOT, YEROKET_ROOT, VIXEN_FETCHCONTENT_CACHE, VIXEN_VULKAN_CACHE_DIR.
# Defaults: the paired Undertow lane worktree (else ~/projects/undertow), ~/projects/Yeroket-Fantasy
# or the current lane's kernel pair, <repo>/.tmp/fetch, and the shared ~/.cache/vixen/vulkan-sdk.
# A build tree is never shared between commits (a stale tree keeps the old Gaia pin).
set -euo pipefail

die() { echo "configure-detached: $*" >&2; exit 2; }

kernel_sha="" worktree="" build_dir="" dry_run=0 sha=""
while [ $# -gt 0 ]; do
    case "$1" in
        --kernel-sha) kernel_sha="${2:?--kernel-sha needs a value}"; shift 2 ;;
        --worktree) worktree="${2:?--worktree needs a value}"; shift 2 ;;
        --build-dir) build_dir="${2:?--build-dir needs a value}"; shift 2 ;;
        --dry-run) dry_run=1; shift ;;
        -h|--help) sed -n '2,13p' "${BASH_SOURCE[0]}"; exit 0 ;;
        -*) die "unknown option: $1" ;;
        *) [ -z "$sha" ] || die "one <sha> only"; sha="$1"; shift ;;
    esac
done
[ -n "$sha" ] || die "usage: configure-detached.sh [options] <sha>"

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
full_sha="$(git -C "$repo" rev-parse --verify --quiet "$sha^{commit}")" || die "not a commit in $repo: $sha"
short_sha="${full_sha:0:8}"
worktree="${worktree:-$repo/.tmp/detached/$short_sha}"
build_dir="${build_dir:-$worktree/build/wsl}"

# Same pairing the VIXEN CMakeLists uses: .claude-worktrees/<lane> -> undertow/.claude-worktrees/<lane>.
lane_name="$(basename "$repo")"
projects_root="$(cd "$repo/../../.." && pwd)"
schema_rel="core/src/Undertow.Authoring/Schema/schemas.json"
if [ -z "${UNDERTOW_ROOT:-}" ]; then
    for candidate in "$projects_root/undertow/.claude-worktrees/$lane_name" "$projects_root/undertow"; do
        if [ -e "$candidate/$schema_rel" ]; then UNDERTOW_ROOT="$candidate"; break; fi
    done
fi
[ -n "${UNDERTOW_ROOT:-}" ] && [ -e "$UNDERTOW_ROOT/$schema_rel" ] || die "no Undertow checkout with $schema_rel; set UNDERTOW_ROOT"
schema_catalog="$UNDERTOW_ROOT/$schema_rel"

codegen_rel="Packages/com.yeroket.utility.kernel-framework/CodegenTool~/CodegenTool.csproj"
if [ -z "${YEROKET_ROOT:-}" ]; then
    for candidate in "$projects_root/Yeroket-Fantasy/.claude-worktrees/$lane_name" "$projects_root/Yeroket-Fantasy"; do
        if [ -e "$candidate/$codegen_rel" ]; then YEROKET_ROOT="$candidate"; break; fi
    done
fi
[ -n "${YEROKET_ROOT:-}" ] && [ -e "$YEROKET_ROOT/$codegen_rel" ] || die "no Yeroket kernel checkout with the CodegenTool; set YEROKET_ROOT"

fetch_cache="${VIXEN_FETCHCONTENT_CACHE:-$repo/.tmp/fetch}"
vulkan_cache="${VIXEN_VULKAN_CACHE_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/vixen/vulkan-sdk}"

cmake_args=(-S "$worktree/VIXEN" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
    "-DVIXEN_VULKAN_CACHE_DIR=$vulkan_cache"
    "-DVIXEN_SCHEMA_CATALOG=$schema_catalog"
    "-DYEROKET_ROOT=$YEROKET_ROOT"
    "-DFETCHCONTENT_BASE_DIR=$fetch_cache")
[ -z "$kernel_sha" ] || cmake_args+=("-DVIXEN_YEROKET_KERNEL_SHA_OVERRIDE=$kernel_sha")

echo "commit:          $full_sha"
echo "worktree:        $worktree"
echo "build dir:       $build_dir"
echo "UNDERTOW_ROOT:   $UNDERTOW_ROOT"
echo "schema catalog:  $schema_catalog"
echo "YEROKET_ROOT:    $YEROKET_ROOT"
echo "fetch cache:     $fetch_cache"
echo "vulkan cache:    $vulkan_cache"
echo "kernel override: ${kernel_sha:-<none; tracked pin>}"
echo "cmake: cmake ${cmake_args[*]}"
[ "$dry_run" -eq 0 ] || exit 0

if [ ! -e "$worktree/.git" ]; then
    mkdir -p "$(dirname "$worktree")"
    git -C "$repo" worktree add --detach "$worktree" "$full_sha"
elif [ "$(git -C "$worktree" rev-parse HEAD)" != "$full_sha" ]; then
    die "$worktree exists at a different commit; pass --worktree or remove it"
fi
mkdir -p "$fetch_cache"
export UNDERTOW_ROOT YEROKET_ROOT
exec cmake "${cmake_args[@]}"
