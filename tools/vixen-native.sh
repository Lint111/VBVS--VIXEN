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
#   ps SCRIPT.ps1 [ARGS]       run a PowerShell script through the declared System32 powershell.exe (no PATH needed);
#                              ps -Command TEXT | ps -Check SCRIPT.ps1 (parse only) also work
#   rasterize SVG PNG [W H]    render an SVG to a PNG with headless Edge. The SVG must sit on a /mnt/<drive> path
#                              (it keeps its relative <image> links); paths are converted to C:/... form for Edge
# --direct skips the native checkout and runs this worktree's own build.bat (it maps the UNC path itself).
# Environment: VIXEN_NATIVE_ROOT (default /mnt/c/vixen-native), VIXEN_NATIVE_SYSTEM32 (cmd.exe directory
# override). UNDERTOW_ROOT, YEROKET_ROOT, VIXEN_BINARY_DIR and the other VIXEN_* knobs documented in
# build.bat are forwarded into the Windows process; the two roots are forwarded as paths.
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

# Windows executables are found by the same rule as cmd.exe: PATH first (WSL interop), then the
# declared System32 directory. A bare name from the WSL PATH exits 127 when interop PATH is trimmed.
find_powershell() {
    local candidate dir
    if candidate="$(command -v powershell.exe 2>/dev/null)"; then echo "$candidate"; return 0; fi
    for dir in "${VIXEN_NATIVE_SYSTEM32:-}" /mnt/c/Windows/System32 /mnt/*/Windows/System32; do
        if [[ -n "$dir" && -x "$dir/WindowsPowerShell/v1.0/powershell.exe" ]]; then echo "$dir/WindowsPowerShell/v1.0/powershell.exe"; return 0; fi
    done
    return 1
}

find_edge() {
    local candidate
    for candidate in "${VIXEN_NATIVE_EDGE:-}" "/mnt/c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe" \
                     "/mnt/c/Program Files/Microsoft/Edge/Application/msedge.exe"; do
        if [[ -n "$candidate" && -x "$candidate" ]]; then echo "$candidate"; return 0; fi
    done
    return 1
}

# WSL path -> Windows path (forward slashes, the form Edge and file:// URLs accept). Only drive mounts
# convert: a WSL-filesystem path hangs or returns ERR_FILE_NOT_FOUND in a Windows process.
win_path() {
    [[ "$1" == /mnt/[a-z]/* ]] || die "$1 is not on a Windows drive (/mnt/<drive>/...); copy it under ${native_base} first"
    wslpath -m "$1"
}

do_ps() {
    local ps_exe script win_script check=0
    ps_exe="$(find_powershell)" || die "powershell.exe not found (PATH has no Windows entries and no System32 directory was found; set VIXEN_NATIVE_SYSTEM32)"
    case "${1:-}" in
        -Command) shift; exec "$ps_exe" -NoProfile -ExecutionPolicy Bypass -Command "$@" ;;
        -Check) check=1; shift ;;
    esac
    script="${1:?ps needs a .ps1 path (or -Command TEXT)}"; shift
    [[ -f "$script" ]] || die "script not found: $script"
    script="$(readlink -f -- "$script")"
    # PowerShell (unlike Edge) reads UNC paths, so a script on the WSL filesystem needs no copy.
    if [[ "$script" == /mnt/[a-z]/* ]]; then win_script="$(wslpath -m "$script")"; else win_script="$(wslpath -w "$script")"; fi
    if [[ "$check" == 1 ]]; then
        exec "$ps_exe" -NoProfile -Command "\$e=\$null; [void][System.Management.Automation.Language.Parser]::ParseFile('$win_script',[ref]\$null,[ref]\$e); if (\$e) { \$e | ForEach-Object { Write-Error \$_.Message }; exit 1 }; Write-Output 'parse ok: $win_script'"
    fi
    exec "$ps_exe" -NoProfile -ExecutionPolicy Bypass -File "$win_script" "$@"
}

do_rasterize() {
    local svg png width="${3:-1500}" height="${4:-}" edge shot_dir shot
    svg="$(readlink -f -- "${1:?rasterize needs SVG PNG [WIDTH HEIGHT]}")"; png="${2:?rasterize needs SVG PNG [WIDTH HEIGHT]}"
    [[ -f "$svg" ]] || die "svg not found: $svg"
    edge="$(find_edge)" || die "msedge.exe not found; set VIXEN_NATIVE_EDGE to its path"
    if [[ -z "$height" ]]; then
        height="$(sed -n 's/.*<svg[^>]* height="\([0-9.]*\)".*/\1/p' "$svg" | head -1)"
        [[ -n "$height" ]] || die "no height attribute on <svg> in $svg; pass WIDTH HEIGHT"
        height="${height%%.*}"
    fi
    # Edge writes the screenshot as a Windows process, so it lands on a drive first and is then copied.
    shot_dir="$native_base/.tmp"; mkdir -p "$shot_dir"
    shot="$shot_dir/rasterize-$$.png"; rm -f "$shot"
    timeout 120 "$edge" --headless --disable-gpu --hide-scrollbars --window-size="$width,$height" \
        --screenshot="$(win_path "$shot")" "file:///$(win_path "$svg")" >/dev/null 2>&1 || true
    [[ -s "$shot" ]] || die "Edge wrote no screenshot for $svg (window ${width}x${height}); check that the file is readable from Windows and retry with explicit WIDTH HEIGHT"
    mkdir -p "$(dirname "$png")"; mv "$shot" "$png"
    say "wrote $png"
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
    local forward="UNDERTOW_ROOT/p:YEROKET_ROOT/p:VIXEN_BINARY_DIR/p:VIXEN_FETCHCONTENT_CACHE/p:VIXEN_MAX_BUILD_JOBS:VIXEN_BUILD_ID:VIXEN_BUILD_LOCK_TIMEOUT:VIXEN_SKIP_BUILD_LOCK:VIXEN_QUEUE_TICKET_ID"
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
        -h|--help) sed -n '2,23p' "${BASH_SOURCE[0]}"; exit 0 ;;
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
    ps) do_ps "$@" ;;
    rasterize) do_rasterize "$@" ;;
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
