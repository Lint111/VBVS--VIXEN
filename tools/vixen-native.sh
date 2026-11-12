#!/usr/bin/env bash
# WSL -> Windows-native route for VIXEN: finds cmd.exe, keeps Windows path quoting intact,
# provisions the per-lane native checkout, syncs it to the lane's committed tip and bootstraps
# the pinned toolchain before the build.
#
# Usage: tools/vixen-native.sh [--lane NAME] [--branch NAME] [--direct] [--log FILE] COMMAND [ARGS]
#   cmd                        print the discovered cmd.exe
#   jobs                       print how the ninja job count is derived (node width capped by memory, R847.4) and the count
#   checkout                   create $VIXEN_NATIVE_ROOT/<lane> as a clone of this worktree if missing
#   sync                       checkout, then fast-forward it to this worktree's branch tip and verify it
#   provision                  run build.bat provision when the pinned Ninja/CMake files are missing
#   prepare                    sync + kernel pin fetch + provision (run this before queueing the build)
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

source "$(dirname "${BASH_SOURCE[0]}")/node-width.sh"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
native_base="${VIXEN_NATIVE_ROOT:-/mnt/c/vixen-native}"
lane="$(basename "$repo_root")"
branch=""
direct=0
log_file=""
cancelled=""
cancelled_signo=0
run_pid=""
pid_file=""

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

# DrvFS reports every file as executable, so a native checkout that inherits core.fileMode=true
# lists thousands of tracked files as mode-changed (T-2947). Set it on this clone's own config only.
ignore_drvfs_filemode() {
    git -C "$1" config core.fileMode false
}

do_checkout() {
    validate_lane
    resolve_branch
    local dir; dir="$(native_dir)"
    if [[ -d "$dir/.git" ]]; then
        ignore_drvfs_filemode "$dir"
        return 0
    fi
    [[ ! -e "$dir" ]] || die "$dir exists but is not a git checkout; refusing to touch it"
    mkdir -p "$native_base"
    say "cloning $branch into $dir"
    git clone --quiet --no-hardlinks --branch "$branch" "$repo_root" "$dir"
    ignore_drvfs_filemode "$dir"
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

# The Windows build cannot probe the WSL layout itself, so export the same roots the Linux route
# derives (VIXEN/codegen/CMakeLists.txt, tools/with-test-lock.sh): the Undertow lane paired with this
# worktree's name, else the sibling Undertow checkout; the Yeroket kernel from $HOME/Github, else
# the projects sibling. Explicit UNDERTOW_ROOT / YEROKET_ROOT always win. run_bat forwards them as paths.
infer_roots() {
    local projects_root candidate
    projects_root="$(cd "$repo_root/../../.." 2>/dev/null && pwd -P)" || return 0
    if [[ -z "${UNDERTOW_ROOT:-}" ]]; then
        for candidate in "$projects_root/undertow/.claude-worktrees/$(basename "$repo_root")" "$projects_root/undertow"; do
            if [[ -f "$candidate/core/src/Undertow.Authoring/Schema/schemas.json" ]]; then
                export UNDERTOW_ROOT="$(cd "$candidate" && pwd -P)"
                say "UNDERTOW_ROOT inferred: $UNDERTOW_ROOT"
                break
            fi
        done
    fi
    if [[ -z "${YEROKET_ROOT:-}" ]]; then
        for candidate in "$HOME/Github/Yeroket-Fantasy" "$projects_root/Yeroket-Fantasy"; do
            if [[ -f "$candidate/Packages/com.yeroket.utility.kernel-framework/CodegenTool~/CodegenTool.csproj" ]]; then
                export YEROKET_ROOT="$(cd "$candidate" && pwd -P)"
                say "YEROKET_ROOT inferred: $YEROKET_ROOT"
                break
            fi
        done
    fi
}

# Peak memory of one cl.exe at the worst moment of a cold -j4 vixen_lookdev_capture build on ws: the
# summed cl.exe working sets divided by the cl.exe count at the instant of the peak (3538 MB / 4 = 885;
# the largest single process was 1203 MB). Measured by reports/sn-nativejobs1-evidence/sample-build.ps1;
# VIXEN_BUILD_MB_PER_JOB overrides it.
BUILD_MB_PER_JOB_DEFAULT=900

# Windows logical cores and available memory (MB) as the build sees them; empty when unreadable.
# shellcheck source=node-width.sh
windows_logical_cores() {
    local n=""
    if need_cmd_quiet; then n="$("$cmd_exe" /c "echo %NUMBER_OF_PROCESSORS%" 2>/dev/null | tr -dc '0-9' || true)"; fi
    echo "${n:-$(nproc)}"
}
windows_available_mb() {
    [[ -z "${VIXEN_NATIVE_AVAILABLE_MB:-}" ]] || { echo "$VIXEN_NATIVE_AVAILABLE_MB"; return 0; }
    local ps_exe
    ps_exe="$(find_powershell)" || return 0
    "$ps_exe" -NoProfile -Command "[int](Get-Counter '\\Memory\\Available MBytes').CounterSamples[0].CookedValue" 2>/dev/null | tr -dc '0-9' || true
}
need_cmd_quiet() { cmd_exe="$(find_cmd)" || return 1; }

# Sets VIXEN_MAX_BUILD_JOBS (an explicit value wins) and logs how it was derived in one line (R847.4):
# node width, capped by memory. jobs = max(1, min(Windows logical cores, (Windows available MB - other
# queue holders' uncommitted memory - headroom) / per_job_mb)). The queue grant sets admission only. It is
# read at the start of every build step (each run_bat), so a step that starts later sees a freer node;
# under the queue the chosen jobs * per_job is published as this holder's claim for the step's duration.
derived_build_jobs=""
resolve_build_jobs() {
    local cpus mem per jobs other holders manual where="direct"
    per="${VIXEN_BUILD_MB_PER_JOB:-$BUILD_MB_PER_JOB_DEFAULT}"
    [[ "$per" =~ ^[1-9][0-9]*$ ]] || die "VIXEN_BUILD_MB_PER_JOB='$per' is not a positive integer"
    # A value this function exported for an earlier step of the same command (provision, then build) is
    # not an override: each step reads the node again.
    if [[ -n "${VIXEN_MAX_BUILD_JOBS:-}" && "$VIXEN_MAX_BUILD_JOBS" != "$derived_build_jobs" ]]; then
        say "build jobs: $VIXEN_MAX_BUILD_JOBS (explicit VIXEN_MAX_BUILD_JOBS)"
        return 0
    fi
    # A manual queue grant (--regrant) is the owner's choice of width: read from the grant file at the start of every step.
    if manual="$(nw_manual_grant_cpus)"; then
        export VIXEN_MAX_BUILD_JOBS="$manual"
        derived_build_jobs="$manual"
        say "build jobs: $manual (source=manual queue grant, re-read at this step; the node width rule is not applied)"
        return 0
    fi
    [[ -z "${UT_QUEUE_HOLDER_PID:-}" ]] || where="queue"
    cpus="$(windows_logical_cores)"
    [[ "$cpus" =~ ^[1-9][0-9]*$ ]] || die "cannot derive the build job count: CPUs '$cpus' is not a positive integer"
    nw_claim_begin
    mem="$(windows_available_mb)"
    if [[ "$mem" =~ ^[0-9]+$ ]]; then
        read -r other holders <<<"$(nw_other_uncommitted_mb)"
        jobs="$(nw_jobs "$cpus" $(( mem > other ? mem - other : 0 )) "$per")"
        nw_claim_end $(( jobs * per )) "${UT_AGENT_ID:-vixen}:native-build"
        mem="$mem other_uncommitted_mb=$other other_holders=$holders"
    else
        jobs="$cpus"
        nw_claim_end 0 "${UT_AGENT_ID:-vixen}:native-build"
        mem="unknown"
    fi
    export VIXEN_MAX_BUILD_JOBS="$jobs"
    derived_build_jobs="$jobs"
    say "build jobs: $jobs (source=$where cores=$cpus windows_available_mb=$mem per_job_mb=$per)"
}

# Base directory the build runs from: the native checkout, or this worktree with --direct.
base_dir() { if [[ "$direct" == 1 ]]; then echo "$repo_root"; else native_dir; fi; }

# cmd.exe re-parses its command line, so an unquoted `|`, `&`, `<`, `>` in an argument (a CTest -R
# regex such as A|B) becomes a command separator before the batch file starts. Interop does not
# quote them, so caret-escape the metacharacters for the command line, and also hand the batch file
# the exact arguments in VIXEN_NATIVE_ARGS (each double-quoted), which it can expand with
# `!VIXEN_NATIVE_ARGS!` under delayed expansion without cmd.exe re-parsing them (a `%1`/`%*`
# expansion is re-parsed and cannot be made safe).
cmd_escape_arg() { local a="$1"; a="${a//^/^^}"; a="${a//&/^&}"; a="${a//|/^|}"; a="${a//</^<}"; a="${a//>/^>}"; echo "$a"; }
quote_arg_list() {
    local out="" a
    for a in "$@"; do out+="\"${a//\"/\\\"}\" "; done
    echo "${out% }"
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
        [[ "$win_arg" != *" "* ]] || die "$win_arg contains spaces; use the native checkout (drop --direct)"
        cd "$(dirname "$cmd_exe")/../.."
    fi
    infer_roots
    # Build status and per-build logs go to the lane's own checkout, not the machine-wide Windows
    # TEMP shared by every concurrent build; queue tickets keep TEMP because build_queue.ps1 reads it.
    if [[ -z "${VIXEN_BUILD_LOG_DIR:-}" && -z "${VIXEN_QUEUE_TICKET_ID:-}" && "$direct" != 1 ]]; then
        export VIXEN_BUILD_LOG_DIR="$(native_dir)/.tmp/native-build"
    fi
    local cmd_args=() a
    for a in "$@"; do cmd_args+=("$(cmd_escape_arg "$a")"); done
    export VIXEN_NATIVE_ARGS="$(quote_arg_list "$@")"
    # Lane builds must not fetch into another lane's cache: default a checkout-local FetchContent cache
    # here (T-2955); an explicit value still wins. The ninja job count is derived from the CPUs and
    # memory this run was granted (resolve_build_jobs), not a fixed number.
    resolve_build_jobs
    export VIXEN_FETCHCONTENT_CACHE="${VIXEN_FETCHCONTENT_CACHE:-$(base_dir)/.tmp/fetch}"
    mkdir -p "$VIXEN_FETCHCONTENT_CACHE"
    local forward="VIXEN_NATIVE_ARGS:UNDERTOW_ROOT/p:YEROKET_ROOT/p:VIXEN_BINARY_DIR/p:VIXEN_FETCHCONTENT_CACHE/p:VIXEN_MAX_BUILD_JOBS:UT_QUEUE_CREDITS:VIXEN_BUILD_LOG_DIR/p:VIXEN_BUILD_ID:VIXEN_BUILD_LOCK_TIMEOUT:VIXEN_SKIP_BUILD_LOCK:VIXEN_QUEUE_TICKET_ID:VIXEN_NATIVE_PIDFILE/p:UT_QUEUE_WINROOT_FILE/p"
    export WSLENV="${WSLENV:+$WSLENV:}$forward"
    mkdir -p "$(dirname "$log_file")"
    # The lock scripts write the Windows process-tree root here (Register-NativeRunRoot.ps1) so a
    # cancel can kill the whole tree and release Global\VixenBuildLock / Global\VixenConfigureLock.
    pid_file="$(dirname "$log_file")/.native-root-$$.pid"
    rm -f "$pid_file"
    export VIXEN_NATIVE_PIDFILE="$pid_file"
    say "running: cmd.exe /c $win_arg ${cmd_args[*]:-} (log: $log_file)"
    # The last link/stage step of a native build is silent and uses almost no WSL-side CPU, which
    # the queue's idle watchdog reads as a stall (T-6207). Say that the Windows process is alive.
    local heartbeat_pid started=$SECONDS
    (while sleep "${VIXEN_NATIVE_HEARTBEAT_S:-30}"; do echo "[vixen-native] heartbeat: $bat_name running $((SECONDS - started))s"; done) &
    heartbeat_pid=$!
    # Background + wait so a SIGTERM/SIGINT/SIGHUP reaches the trap at once; bash defers traps
    # until a foreground pipeline ends, and the interop client exiting does not stop the
    # Windows children (T-4910).
    cancelled=""
    cancelled_signo=0
    ( "$cmd_exe" /c "$win_arg" "${cmd_args[@]}" 2>&1 | tee "$log_file"; exit "${PIPESTATUS[0]}" ) &
    run_pid=$!
    trap 'cancel_native TERM' TERM
    trap 'cancel_native INT' INT
    trap 'cancel_native HUP' HUP
    set +e
    wait "$run_pid"
    rc=$?
    # A trapped signal interrupts the first wait; the second collects the real status.
    if [[ -n "$cancelled" ]]; then wait "$run_pid" 2>/dev/null; rc=$((128 + cancelled_signo)); fi
    set -e
    trap - TERM INT HUP
    kill "$heartbeat_pid" 2>/dev/null || true
    wait "$heartbeat_pid" 2>/dev/null || true
    nw_claim_release
    rm -f "$pid_file"
    say "exit code $rc"
    return "$rc"
}

# Stops the Windows process tree of the running build (cmake, ninja, cl.exe and the lock-holding
# PowerShell) and then the local relay, so a cancelled queue job leaves no build lock behind.
cancel_native() {
    local sig="$1" root taskkill
    cancelled="$sig"
    case "$sig" in HUP) cancelled_signo=1 ;; INT) cancelled_signo=2 ;; *) cancelled_signo=15 ;; esac
    say "received SIG$sig; stopping the Windows build process tree"
    taskkill="$(dirname "$cmd_exe")/taskkill.exe"
    root=""
    [[ -s "$pid_file" ]] && root="$(tr -dc '0-9' < "$pid_file")"
    if [[ -z "$root" ]]; then
        say "WARNING: no Windows process tree was registered ($pid_file missing), so it cannot be killed from here." \
            "If Global\\VixenBuildLock stays held: taskkill /PID <the stuck cmd.exe or powershell.exe> /T /F"
    elif [[ ! -x "$taskkill" ]]; then
        say "WARNING: $taskkill not found; run 'taskkill /PID $root /T /F' on Windows to release the build lock"
    elif "$taskkill" /PID "$root" /T /F >/dev/null 2>&1; then
        say "killed Windows process tree rooted at PID $root"
    else
        say "taskkill /PID $root /T /F failed or the tree had already exited"
    fi
    kill "$run_pid" 2>/dev/null || true
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

# A YEROKET_ROOT clone (the lane's native kernel snapshot) that predates the tracked pin makes the
# configure fail; fetch the pin from the sibling Yeroket checkout (VIXEN_YEROKET_PIN_SOURCE, else
# ~/projects or ~/Github Yeroket-Fantasy) before it runs.
do_kernel_pin() {
    local root="${YEROKET_ROOT:-}" pin source candidate
    [[ -n "$root" && -e "$root/.git" ]] || return 0
    pin="$(sed -n 's/^set(_vixen_tracked_yeroket_kernel_sha "\([0-9a-f]\{40\}\)").*/\1/p' "$(base_dir)/VIXEN/codegen/CMakeLists.txt")"
    [[ -n "$pin" ]] || die "no tracked kernel pin (_vixen_tracked_yeroket_kernel_sha) in $(base_dir)/VIXEN/codegen/CMakeLists.txt"
    if git -C "$root" cat-file -e "$pin^{commit}" 2>/dev/null; then return 0; fi
    for candidate in "${VIXEN_YEROKET_PIN_SOURCE:-}" "$HOME/projects/Yeroket-Fantasy" "$HOME/Github/Yeroket-Fantasy"; do
        if [[ -n "$candidate" && "$candidate" != "$root" ]] && git -C "$candidate" cat-file -e "$pin^{commit}" 2>/dev/null; then source="$candidate"; break; fi
    done
    [[ -n "${source:-}" ]] || die "YEROKET_ROOT $root lacks tracked kernel pin $pin and no sibling checkout has it; fetch it into one of them or set VIXEN_YEROKET_PIN_SOURCE"
    say "YEROKET_ROOT lacks tracked kernel pin ${pin:0:12}; fetching it from $source"
    git -C "$root" -c uploadpack.allowAnySHA1InWant=true fetch --quiet --no-tags "$source" "$pin" \
        || die "could not fetch kernel pin $pin from $source into $root"
    git -C "$root" cat-file -e "$pin^{commit}" 2>/dev/null || die "kernel pin $pin is still missing from $root after the fetch from $source"
}

do_prepare() {
    if [[ "$direct" != 1 ]]; then do_sync; fi
    do_kernel_pin
    do_provision
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --lane) lane="${2:?--lane needs a name}"; shift 2 ;;
        --branch) branch="${2:?--branch needs a name}"; shift 2 ;;
        --direct) direct=1; shift ;;
        --log) log_file="${2:?--log needs a file}"; shift 2 ;;
        -h|--help) sed -n '2,24p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) break ;;
    esac
done
[[ $# -gt 0 ]] || die "missing command (cmd|checkout|sync|provision|prepare|build|bat); see --help"
command_name="$1"; shift
# The native checkout is only a sync target. Run from inside it, the wrapper would sync the clone to
# itself and silently build whatever commit the clone already holds instead of the lane's tip.
if [[ "$direct" != 1 && "$command_name" != cmd ]]; then
    native_real="$(cd "$native_base" 2>/dev/null && pwd -P || true)"
    if [[ -n "$native_real" && "$repo_root/" == "$native_real/"* ]]; then
        die "run from the source worktree, not the native clone ($repo_root is under $native_real): the wrapper there would sync the clone to itself and build a stale commit; use the lane worktree's tools/vixen-native.sh (or --direct to build this checkout as is)"
    fi
fi
if [[ -z "$log_file" ]]; then
    log_file="$repo_root/.tmp/vixen-native/$lane-$command_name-$(date +%Y%m%d-%H%M%S).log"
fi

case "$command_name" in
    cmd) need_cmd; echo "$cmd_exe" ;;
    jobs) resolve_build_jobs; nw_claim_release; echo "$VIXEN_MAX_BUILD_JOBS" ;;
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
