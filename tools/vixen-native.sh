#!/usr/bin/env bash
# WSL -> Windows-native route for VIXEN: finds cmd.exe even when the shell PATH has no Windows
# entries (SSH shells) and keeps Windows path quoting intact.
#
# Usage: tools/vixen-native.sh [--log FILE] COMMAND [ARGS]
#   cmd                        print the discovered cmd.exe
#   build [ACTION [PRESET [TARGET]]]
#                              run this worktree's build.bat (ACTION defaults to build.bat's own default)
#   bat RELPATH [ARGS]         run a batch file of this worktree (e.g. a direct vcvars64 route)
# Environment: VIXEN_NATIVE_SYSTEM32 (cmd.exe directory override). UNDERTOW_ROOT, VIXEN_BINARY_DIR and
# the other VIXEN_* knobs documented in build.bat are forwarded into the Windows process.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
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
        [[ "$win_arg" != *" "* ]] || die "$win_arg contains spaces; use a checkout on a Windows drive"
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

while [[ $# -gt 0 ]]; do
    case "$1" in
        --log) log_file="${2:?--log needs a file}"; shift 2 ;;
        -h|--help) sed -n '2,13p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) break ;;
    esac
done
[[ $# -gt 0 ]] || die "missing command (cmd|build|bat); see --help"
command_name="$1"; shift
if [[ -z "$log_file" ]]; then
    log_file="$repo_root/.tmp/vixen-native/$command_name-$(date +%Y%m%d-%H%M%S).log"
fi

case "$command_name" in
    cmd) need_cmd; echo "$cmd_exe" ;;
    build) run_bat "$repo_root/build.bat" "$@" ;;
    bat)
        [[ $# -ge 1 ]] || die "bat needs a path relative to the worktree"
        rel="$1"; shift
        [[ "$rel" != /* && "$rel" != *..* ]] || die "bat path must be relative to the worktree and stay inside it"
        run_bat "$repo_root/$rel" "$@"
        ;;
    *) die "unknown command '$command_name'" ;;
esac
