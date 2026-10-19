#!/usr/bin/env bash
# WSL -> Windows-native route for VIXEN: finds cmd.exe, keeps Windows path quoting intact,
# provisions the per-lane native checkout, syncs it to the lane's committed tip and bootstraps
# the pinned toolchain before the build.
#
# Usage: tools/vixen-native.sh [--lane NAME] [--branch NAME] [--direct] [--log FILE] COMMAND [ARGS]
#   cmd                        print the discovered cmd.exe
#   checkout                   create $VIXEN_NATIVE_ROOT/<lane> as a clone of this worktree if missing
#   sync                       checkout, then fast-forward it to this worktree's branch tip and verify it
#   provision                  run build.bat provision when the pinned Ninja/CMake files are missing
#   prepare                    sync + provision (run this before queueing the build)
#   build [ACTION [PRESET [TARGET]]]
#                              prepare, then build.bat (ACTION defaults to build.bat's own default)
#   bat RELPATH [ARGS]         prepare, then run a batch file of the checkout (e.g. a direct vcvars64 route)
# --direct skips the native checkout and runs this worktree's own build.bat (it maps the UNC path itself).
# Environment: VIXEN_NATIVE_ROOT (default /mnt/c/vixen-native), VIXEN_NATIVE_SYSTEM32 (cmd.exe directory
# override). UNDERTOW_ROOT, VIXEN_BINARY_DIR and the other VIXEN_* knobs documented in build.bat are
# forwarded into the Windows process.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
native_base="${VIXEN_NATIVE_ROOT:-/mnt/c/vixen-native}"
lane="$(basename "$repo_root")"
branch=""
direct=0
log_file=""

die() { echo "[vixen-native] ERROR: $*" >&2; exit 2; }
say() { echo "[vixen-native] $*"; }

find_cmd() {
    local candidate dir
    if candidate="$(command -v cmd.exe 2>/dev/null)"; then echo "$candidate"; return 0; fi
    for dir in "${VIXEN_NATIVE_SYSTEM32:-}" /mnt/c/Windows/System32 /mnt/*/Windows/System32; do
        if [[ -n "$dir" && -x "$dir/cmd.exe" ]]; then echo "$dir/cmd.exe"; return 0; fi
    done
    return 1
}

need_cmd() {
    cmd_exe="$(find_cmd)" || die "cmd.exe not found (PATH has no Windows entries and no System32 directory was found; set VIXEN_NATIVE_SYSTEM32)"
}

native_dir() { echo "$native_base/$lane"; }

validate_lane() {
    [[ "$lane" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ ]] || die "lane '$lane' is not a plain directory name"
}

resolve_branch() {
    if [[ -z "$branch" ]]; then
        branch="$(git -C "$repo_root" symbolic-ref --short -q HEAD)" || die "this worktree is on a detached HEAD; pass --branch"
    fi
    git -C "$repo_root" rev-parse --verify -q "refs/heads/$branch" >/dev/null || die "no local branch '$branch' in $repo_root"
}

do_checkout() {
    validate_lane
    resolve_branch
    local dir; dir="$(native_dir)"
    if [[ -d "$dir/.git" ]]; then return 0; fi
    [[ ! -e "$dir" ]] || die "$dir exists but is not a git checkout; refusing to touch it"
    mkdir -p "$native_base"
    say "cloning $branch into $dir"
    git clone --quiet --no-hardlinks --branch "$branch" "$repo_root" "$dir"
}

do_sync() {
    do_checkout
    local dir tip head tree_src tree_native
    dir="$(native_dir)"
    if [[ -n "$(git -C "$repo_root" status --porcelain --untracked-files=no)" ]]; then
        say "WARNING: $repo_root has uncommitted changes; only the committed tip is synchronized"
    fi
    tip="$(git -C "$repo_root" rev-parse "refs/heads/$branch")"
    git -C "$dir" -c core.fileMode=false diff --quiet HEAD -- || die "$dir has uncommitted tracked changes; refusing to move it"
    # A raw SHA is not an advertised ref, so fetch the branch ref and move to its tip.
    git -C "$dir" fetch --quiet --no-tags "$repo_root" "+refs/heads/$branch:refs/remotes/vixen-lane/$branch"
    if [[ "$(git -C "$dir" symbolic-ref --short -q HEAD || true)" != "$branch" ]]; then
        git -C "$dir" checkout --quiet -B "$branch" "refs/remotes/vixen-lane/$branch" || die "could not switch $dir to $branch"
    else
        git -C "$dir" merge --quiet --ff-only "refs/remotes/vixen-lane/$branch" || die "$dir has diverged from $branch (not a fast-forward)"
    fi
    head="$(git -C "$dir" rev-parse HEAD)"
    [[ "$head" == "$tip" ]] || die "$dir is at $head, expected $tip"
    git -C "$dir" reset --quiet --mixed HEAD
    tree_src="$(git -C "$repo_root" rev-parse "$tip^{tree}")"
    tree_native="$(git -C "$dir" rev-parse "HEAD^{tree}")"
    [[ "$tree_src" == "$tree_native" ]] || die "tree mismatch after sync: $tree_src vs $tree_native"
    git -C "$dir" -c core.fileMode=false diff --quiet HEAD -- || die "$dir worktree differs from $tip after sync"
    say "synced $dir to ${tip:0:12} ($branch, tree ${tree_native:0:12} verified)"
}

# Base directory the build runs from: the native checkout, or this worktree with --direct.
base_dir() { if [[ "$direct" == 1 ]]; then echo "$repo_root"; else native_dir; fi; }

# Runs a batch file with its own directory as the working directory, so the name carries no
# Windows path (a path with spaces or backslashes is mangled by interop) and cmd.exe never
# starts on a UNC directory. A UNC-only base falls back to the drive root and the full path.
run_bat() {
    local bat="$1"; shift
    local bat_dir bat_name win_arg rc
    need_cmd
    bat_dir="$(dirname "$bat")"; bat_name="$(basename "$bat")"
    [[ -f "$bat" ]] || die "batch file not found: $bat"
    if [[ "$bat_dir" == /mnt/[a-z]/* ]]; then
        win_arg=".\\$bat_name"
        cd "$bat_dir"
    else
        win_arg="$(wslpath -w "$bat")"
        [[ "$win_arg" != *" "* ]] || die "$win_arg contains spaces; use the native checkout (drop --direct)"
        cd "$(dirname "$cmd_exe")/../.."
    fi
    local forward="UNDERTOW_ROOT/p:VIXEN_BINARY_DIR/p:VIXEN_FETCHCONTENT_CACHE/p:VIXEN_MAX_BUILD_JOBS:VIXEN_BUILD_ID:VIXEN_BUILD_LOCK_TIMEOUT:VIXEN_SKIP_BUILD_LOCK:VIXEN_QUEUE_TICKET_ID"
    export WSLENV="${WSLENV:+$WSLENV:}$forward"
    mkdir -p "$(dirname "$log_file")"
    say "running: cmd.exe /c $win_arg $* (log: $log_file)"
    set +e
    "$cmd_exe" /c "$win_arg" "$@" 2>&1 | tee "$log_file"
    rc="${PIPESTATUS[0]}"
    set -e
    say "exit code $rc"
    return "$rc"
}

do_provision() {
    local dir tool missing=0
    dir="$(base_dir)"
    for tool in ninja-bin.txt cmake-exe.txt; do
        [[ -f "$dir/VIXEN/.win-native-toolchain/$tool" ]] || missing=1
    done
    if [[ "$missing" == 0 ]]; then say "native toolchain already provisioned in $dir"; return 0; fi
    say "native toolchain pins missing in $dir; running build.bat provision"
    run_bat "$dir/build.bat" provision || {
        local rc=$?
        say "provision stopped with exit code $rc (20 = needs an elevated PowerShell, 30 = Vulkan SDK version decision); see the log"
        return "$rc"
    }
}

do_prepare() {
    if [[ "$direct" != 1 ]]; then do_sync; fi
    do_provision
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --lane) lane="${2:?--lane needs a name}"; shift 2 ;;
        --branch) branch="${2:?--branch needs a name}"; shift 2 ;;
        --direct) direct=1; shift ;;
        --log) log_file="${2:?--log needs a file}"; shift 2 ;;
        -h|--help) sed -n '2,19p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) break ;;
    esac
done
[[ $# -gt 0 ]] || die "missing command (cmd|checkout|sync|provision|prepare|build|bat); see --help"
command_name="$1"; shift
if [[ -z "$log_file" ]]; then
    log_file="$repo_root/.tmp/vixen-native/$lane-$command_name-$(date +%Y%m%d-%H%M%S).log"
fi

case "$command_name" in
    cmd) need_cmd; echo "$cmd_exe" ;;
    checkout) do_checkout ;;
    sync) do_sync ;;
    provision) if [[ "$direct" != 1 ]]; then do_sync; fi; do_provision ;;
    prepare) do_prepare ;;
    build) do_prepare; run_bat "$(base_dir)/build.bat" "$@" ;;
    bat)
        [[ $# -ge 1 ]] || die "bat needs a path relative to the checkout"
        rel="$1"; shift
        [[ "$rel" != /* && "$rel" != *..* ]] || die "bat path must be relative to the checkout and stay inside it"
        do_prepare
        run_bat "$(base_dir)/$rel" "$@"
        ;;
    *) die "unknown command '$command_name'" ;;
esac
