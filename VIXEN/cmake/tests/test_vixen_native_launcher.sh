#!/usr/bin/env bash
# tools/vixen-native.sh against a fake System32 (cmd.exe stub) and throwaway git repositories:
# command discovery, per-lane checkout, sync to the committed tip, provisioning and exit codes.
# Usage: test_vixen_native_launcher.sh <repo-root> <scratch-dir>
set -uo pipefail

repo_root="${1:?repo root}"
scratch="${2:?scratch dir}"
rm -rf "$scratch"
mkdir -p "$scratch"
scratch="$(cd "$scratch" && pwd)"
# Sizing inputs come from this test's fixtures only: a test that runs inside a queue job inherits the live job's
# UT_QUEUE_* variables (holder pid, grant file, other holders' reserved MB, ...) and must not read them.
while IFS= read -r _v; do unset "$_v"; done < <(compgen -e | grep -E '^(UT_QUEUE_|NW_|VIXEN_MAX_BUILD_JOBS$|VIXEN_BUILD_MB_PER_JOB$|VIXEN_NATIVE_AVAILABLE_MB$)' || true)
failures=0

check() {
    local name="$1"; shift
    if "$@"; then echo "PASS $name"; else echo "FAIL $name"; failures=$((failures + 1)); fi
}

fake="$scratch/fake/Windows/System32"
mkdir -p "$fake"
cat > "$fake/cmd.exe" <<'STUB'
#!/usr/bin/env bash
# The build-width probe (echo %NUMBER_OF_PROCESSORS%) answers with the fake core count and is not a build call.
if [[ "$*" == *NUMBER_OF_PROCESSORS* ]]; then echo "${FAKE_CORES:-8}"; exit 0; fi
printf '%s\n' "$@" > "$FAKE_ARGV"
echo "$*" >> "$FAKE_CALLS"
pwd > "$FAKE_CWD"
printf '%s\n' "${WSLENV-}" > "$FAKE_WSLENV"
printf '%s %s\n' "${VIXEN_MAX_BUILD_JOBS-}" "${VIXEN_FETCHCONTENT_CACHE-}" > "$FAKE_LANE_ENV"
echo "fake cmd output"
if [[ -n "${FAKE_HANG-}" ]]; then
    # Stands in for the build tree: registers its PID like Register-NativeRunRoot.ps1, then blocks.
    echo "$$" > "$VIXEN_NATIVE_PIDFILE"
    : > "$FAKE_STARTED"
    while true; do sleep 0.1; done
fi
exit "${FAKE_RC:-0}"
STUB
chmod +x "$fake/cmd.exe"
cat > "$fake/taskkill.exe" <<'STUB'
#!/usr/bin/env bash
echo "$*" >> "$FAKE_TASKKILL"
[[ "$1" == /PID ]] && kill -9 "$2" 2>/dev/null
exit 0
STUB
chmod +x "$fake/taskkill.exe"
export FAKE_TASKKILL="$scratch/taskkill.txt" FAKE_STARTED="$scratch/started.flag"
export FAKE_ARGV="$scratch/argv.txt" FAKE_CWD="$scratch/cwd.txt" FAKE_CALLS="$scratch/calls.txt"
export FAKE_WSLENV="$scratch/wslenv.txt" FAKE_LANE_ENV="$scratch/laneenv.txt"
export VIXEN_NATIVE_SYSTEM32="$fake"
# Build width inputs are fixed: 8 Windows cores, 20000 MB available, an empty queue state dir (no other holders).
mkdir -p "$scratch/state"
export VIXEN_NATIVE_AVAILABLE_MB=20000 UT_QUEUE_STATE_DIR="$scratch/state"
# A PATH with no Windows entries, as in an SSH shell.
export PATH="/usr/bin:/bin"

lane_repo="$scratch/lane-a"
mkdir -p "$lane_repo/tools" "$lane_repo/VIXEN"
cp "$repo_root/tools/vixen-native.sh" "$lane_repo/tools/vixen-native.sh"
cp "$repo_root/tools/node-width.sh" "$lane_repo/tools/node-width.sh"
git -C "$lane_repo" init -q -b lane-a
git -C "$lane_repo" config user.email test@example.invalid
git -C "$lane_repo" config user.name test
printf '@echo off\n' > "$lane_repo/build.bat"
printf 'one\n' > "$lane_repo/VIXEN/file.txt"
git -C "$lane_repo" add -A
git -C "$lane_repo" commit -q -m first
launcher="$lane_repo/tools/vixen-native.sh"
native="$scratch/native"
export VIXEN_NATIVE_ROOT="$native"

check "cmd.exe found through the System32 override when PATH lacks it" \
    test "$("$launcher" cmd)" = "$fake/cmd.exe"

check "checkout creates only this lane's directory" \
    bash -c '"$0" checkout >/dev/null && test -d "$1/lane-a/.git" && test "$(ls "$1" | wc -l)" = 1' "$launcher" "$native"

check "a lane name that is not a plain directory name is refused" \
    bash -c '! "$0" --lane ../escape checkout >/dev/null 2>&1' "$launcher"

printf 'two\n' > "$lane_repo/VIXEN/file.txt"
git -C "$lane_repo" commit -q -am second
tip="$(git -C "$lane_repo" rev-parse HEAD)"
check "sync fast-forwards the existing checkout to the committed tip" \
    bash -c '"$0" sync >/dev/null && test "$(git -C "$1/lane-a" rev-parse HEAD)" = "$2" && test "$(cat "$1/lane-a/VIXEN/file.txt")" = two' "$launcher" "$native" "$tip"

printf 'dirty\n' > "$native/lane-a/VIXEN/file.txt"
git -C "$lane_repo" commit -q --allow-empty -m third
check "sync refuses a checkout with uncommitted tracked changes and leaves it alone" \
    bash -c '! "$0" sync >/dev/null 2>&1 && test "$(cat "$1/lane-a/VIXEN/file.txt")" = dirty' "$launcher" "$native"
git -C "$native/lane-a" checkout -q -- VIXEN/file.txt

git -C "$native/lane-a" -c user.email=t@example.invalid -c user.name=t commit -q --allow-empty -m native-only
check "sync refuses a diverged checkout" \
    bash -c '! "$0" sync >/dev/null 2>&1' "$launcher"
git -C "$native/lane-a" reset -q --hard HEAD~1

check "build provisions when the pinned files are missing, then runs build.bat" \
    bash -c ': > "$FAKE_CALLS"; "$0" build all vixen-ninja VixenApp >/dev/null; mapfile -t c < "$FAKE_CALLS"; test "${#c[@]}" = 2 && [[ "${c[0]}" == "/c "*"build.bat provision" ]] && [[ "${c[1]}" == "/c "*"build.bat all vixen-ninja VixenApp" ]]' "$launcher"
mkdir -p "$native/lane-a/VIXEN/.win-native-toolchain"
touch "$native/lane-a/VIXEN/.win-native-toolchain/ninja-bin.txt" "$native/lane-a/VIXEN/.win-native-toolchain/cmake-exe.txt"
check "build passes the action, preset and target to build.bat with a Windows batch path" \
    bash -c '"$0" build all vixen-ninja VixenApp >/dev/null; mapfile -t a < "$FAKE_ARGV"; test "${a[0]}" = /c && [[ "${a[1]}" == *build.bat ]] && [[ "${a[1]}" == *\\* ]] && test "${a[2]}" = all && test "${a[3]}" = vixen-ninja && test "${a[4]}" = VixenApp' "$launcher"
check "provision then build: each step re-derives the width; only a caller-set value is an override" \
    bash -c 'rm -rf "$1/lane-a/VIXEN/.win-native-toolchain"; out="$("$0" build all vixen-ninja VixenApp 2>&1)"; test "$(grep -c "build jobs: 8 (source=" <<<"$out")" = 2 && ! grep -q "explicit VIXEN_MAX_BUILD_JOBS" <<<"$out"' "$launcher" "$native"
check "an argument with spaces stays one argument" \
    bash -c '"$0" bat build.bat "two words" >/dev/null; mapfile -t a < "$FAKE_ARGV"; test "${#a[@]}" = 3 && test "${a[2]}" = "two words"' "$launcher"
check "YEROKET_ROOT is forwarded into the Windows process as a path" \
    bash -c 'YEROKET_ROOT=/home/x/Yeroket-Fantasy "$0" bat build.bat >/dev/null; tr ":" "\n" < "$FAKE_WSLENV" | grep -qx "YEROKET_ROOT/p"' "$launcher"
check "build sizes the job count at node width (8 cores) and defaults a checkout-local fetch cache (R847.4, T-2955)" \
    bash -c '"$0" build >/dev/null; test "$(cat "$FAKE_LANE_ENV")" = "8 $1/lane-a/.tmp/fetch" && test -d "$1/lane-a/.tmp/fetch"' "$launcher" "$native"
check "a one-token queue grant does not narrow the job count" \
    bash -c 'UT_QUEUE_CREDITS=1 UT_QUEUE_CPUS=2 UT_QUEUE_GRANTED_CPUS=2 "$0" jobs 2>/dev/null | tail -1 | grep -qx 8' "$launcher"
check "available memory caps the job count: (3000 - 1024 headroom) / 900 = 2" \
    bash -c 'VIXEN_NATIVE_AVAILABLE_MB=3000 "$0" jobs 2>/dev/null | tail -1 | grep -qx 2' "$launcher"
check "another holder's unspent reservation (Linux-side admission) does not come off the Windows budget" \
    bash -c 'sleep 30 & other=$!; trap "kill $other" EXIT
        t="$(sed "s/.*) //" /proc/$other/stat | awk "{print \$20}")"
        printf "9000\npid=%s\ncurrent=1000\nstart_ticks=%s\n" "$other" "$t" > "$UT_QUEUE_STATE_DIR/undertow-box-memory-reservation.$other"
        # (20000 - 1024) / 4000 = 4; the 8000 unspent MB is not subtracted
        out="$(VIXEN_BUILD_MB_PER_JOB=4000 "$0" jobs 2>&1)"; rm -f "$UT_QUEUE_STATE_DIR/undertow-box-memory-reservation.$other"
        echo "$out" | tail -1 | grep -qx 4 && echo "$out" | grep -q "other_claims_mb=0 other_holders=0"' "$launcher"
check "a claim older than the ramp window is not subtracted (the build is already in the Windows counter)" \
    bash -c 'sleep 30 & other=$!; trap "kill $other" EXIT
        t="$(sed "s/.*) //" /proc/$other/stat | awk "{print \$20}")"
        printf "39528\npid=%s\nstart_ticks=%s\n" "$other" "$t" > "$UT_QUEUE_STATE_DIR/undertow-width-claim.$other"
        touch -d "10 minutes ago" "$UT_QUEUE_STATE_DIR/undertow-width-claim.$other"
        out="$(UT_QUEUE_HOLDER_PID=$$ VIXEN_NATIVE_AVAILABLE_MB=6456 "$0" jobs 2>&1)"; rm -f "$UT_QUEUE_STATE_DIR/undertow-width-claim.$other"
        echo "$out" | tail -1 | grep -qx 6 && echo "$out" | grep -q "other_claims_mb=0 other_holders=0"' "$launcher"
check "another native build's width claim comes off the budget" \
    bash -c 'sleep 30 & other=$!; trap "kill $other" EXIT
        t="$(sed "s/.*) //" /proc/$other/stat | awk "{print \$20}")"
        printf "8000\npid=%s\nstart_ticks=%s\n" "$other" "$t" > "$UT_QUEUE_STATE_DIR/undertow-width-claim.$other"
        # 20000 - 8000 claimed = 12000; (12000 - 1024) / 4000 = 2
        out="$(VIXEN_BUILD_MB_PER_JOB=4000 "$0" jobs 2>&1)"; rm -f "$UT_QUEUE_STATE_DIR/undertow-width-claim.$other"
        echo "$out" | tail -1 | grep -qx 2 && echo "$out" | grep -q "other_claims_mb=8000 other_holders=1"' "$launcher"
check "sn-landvx5 23:07 fixture (windows 8019 MB, 7 holders, 45589 MB Linux-side unspent) sizes 7 jobs, not 1" \
    bash -c 'pids=(); for i in 1 2 3 4 5 6 7; do sleep 30 & pids+=($!); done; trap "kill ${pids[*]}" EXIT
        for p in "${pids[@]}"; do t="$(sed "s/.*) //" /proc/$p/stat | awk "{print \$20}")"
            printf "10000\npid=%s\ncurrent=3500\nstart_ticks=%s\n" "$p" "$t" > "$UT_QUEUE_STATE_DIR/undertow-box-memory-reservation.$p"; done
        out="$(UT_QUEUE_HOLDER_PID=$$ VIXEN_NATIVE_AVAILABLE_MB=8019 UT_QUEUE_RESERVED_OTHERS_MB=45589 "$0" jobs 2>&1)"
        rm -f "$UT_QUEUE_STATE_DIR"/undertow-box-memory-reservation.*
        echo "$out" | tail -1 | grep -qx 7 && echo "$out" | grep -q "windows_available_mb=8019 other_claims_mb=0"' "$launcher"
check "a queue-admitted build keeps the 4-job floor while Windows memory covers 4 * per_job" \
    bash -c 'UT_QUEUE_HOLDER_PID=$$ VIXEN_NATIVE_AVAILABLE_MB=3700 "$0" jobs 2>/dev/null | tail -1 | grep -qx 4' "$launcher"
check "the floor yields when Windows memory is below 4 * per_job (3500 < 3600 -> 2 jobs by memory)" \
    bash -c 'UT_QUEUE_HOLDER_PID=$$ VIXEN_NATIVE_AVAILABLE_MB=3500 "$0" jobs 2>/dev/null | tail -1 | grep -qx 2' "$launcher"
check "a queue holder claims its width for the build and releases it afterwards" \
    bash -c 'UT_QUEUE_HOLDER_PID=$$ "$0" build >/dev/null; test ! -e "$UT_QUEUE_STATE_DIR/undertow-width-claim.$$"' "$launcher"
check "explicit job cap and fetch cache are kept" \
    bash -c 'VIXEN_MAX_BUILD_JOBS=2 VIXEN_FETCHCONTENT_CACHE=/tmp/x-fetch-$$ "$0" build >/dev/null; test "$(cat "$FAKE_LANE_ENV")" = "2 /tmp/x-fetch-$$"; rmdir /tmp/x-fetch-$$' "$launcher"
check "bat refuses a path that leaves the worktree" \
    bash -c '! "$0" bat ../elsewhere.bat >/dev/null 2>&1' "$launcher"
check "build exit code is the batch file's exit code" \
    bash -c 'FAKE_RC=7 "$0" build >/dev/null; test $? = 7' "$launcher"
check "build output is kept in the log" \
    bash -c 'log="$1/build.log"; "$0" --log "$log" build >/dev/null; grep -q "fake cmd output" "$log"' "$launcher" "$scratch"
check "a provisioning stop (exit 20) stops the build before build.bat runs" \
    bash -c 'rm -rf "$1/lane-a/VIXEN/.win-native-toolchain"; : > "$FAKE_CALLS"; FAKE_RC=20 "$0" build all >/dev/null; rc=$?; test $rc = 20 && test "$(wc -l < "$FAKE_CALLS")" = 1 && grep -q "build.bat provision" "$FAKE_CALLS"' "$launcher" "$native"
check "a silent batch step is covered by a heartbeat line (T-6207)" \
    bash -c 'sed -i "s/^exit /sleep \${FAKE_SLEEP:-0}; exit /" "$1/cmd.exe"; FAKE_SLEEP=3 VIXEN_NATIVE_HEARTBEAT_S=1 "$0" bat build.bat | grep -q "heartbeat: build.bat running"' "$launcher" "$fake"

# Kernel pin: a YEROKET_ROOT clone that lacks the tracked pin is topped up from the sibling checkout.
sibling="$scratch/kernel-sibling"
git init -q -b main "$sibling"
git -C "$sibling" -c user.email=t@example.invalid -c user.name=t commit -q --allow-empty -m old
git clone -q "$sibling" "$scratch/kernel-snapshot"
git -C "$sibling" -c user.email=t@example.invalid -c user.name=t commit -q --allow-empty -m pinned
pin="$(git -C "$sibling" rev-parse HEAD)"
mkdir -p "$lane_repo/VIXEN/codegen"
printf 'set(_vixen_tracked_yeroket_kernel_sha "%s")\n' "$pin" > "$lane_repo/VIXEN/codegen/CMakeLists.txt"
git -C "$lane_repo" add -A
git -C "$lane_repo" commit -q -m pin
mkdir -p "$native/lane-a/VIXEN/.win-native-toolchain"
check "prepare fetches the tracked kernel pin into a YEROKET_ROOT clone that lacks it" \
    bash -c '! git -C "$2" cat-file -e "$3^{commit}" 2>/dev/null && YEROKET_ROOT="$2" VIXEN_YEROKET_PIN_SOURCE="$1" "$0" prepare >/dev/null && git -C "$2" cat-file -e "$3^{commit}"' "$launcher" "$sibling" "$scratch/kernel-snapshot" "$pin"
check "prepare leaves a YEROKET_ROOT clone that already has the pin untouched" \
    bash -c 'YEROKET_ROOT="$2" VIXEN_YEROKET_PIN_SOURCE=/nonexistent "$0" prepare 2>&1 | { ! grep -q "fetching it"; }' "$launcher" "$sibling" "$scratch/kernel-snapshot"
git clone -q "$scratch/kernel-snapshot" "$scratch/kernel-stale"
git -C "$scratch/kernel-stale" reset -q --hard HEAD~1 2>/dev/null
git -C "$scratch/kernel-stale" -c user.email=t@example.invalid -c user.name=t commit -q --allow-empty -m stale-only
git -C "$scratch/kernel-stale" gc -q --prune=now 2>/dev/null
check "prepare names the pin and the fix when no sibling checkout has it" \
    bash -c 'HOME=/nonexistent YEROKET_ROOT="$1" "$0" prepare 2>&1 | grep -q "VIXEN_YEROKET_PIN_SOURCE"' "$launcher" "$scratch/kernel-stale"

# Cancellation (T-4910): SIGTERM must taskkill the registered Windows tree and exit 128+15.
cancel_case() {
    local sig="$1" want="$2" lpid i rc
    rm -f "$FAKE_TASKKILL" "$FAKE_STARTED"
    FAKE_HANG=1 "$launcher" --log "$scratch/cancel.log" bat build.bat >/dev/null 2>&1 &
    lpid=$!
    for i in $(seq 1 100); do [[ -e "$FAKE_STARTED" ]] && break; sleep 0.1; done
    [[ -e "$FAKE_STARTED" ]] || { kill -9 "$lpid" 2>/dev/null; return 1; }
    kill -"$sig" "$lpid"
    for i in $(seq 1 100); do kill -0 "$lpid" 2>/dev/null || break; sleep 0.1; done
    kill -0 "$lpid" 2>/dev/null && { kill -9 "$lpid"; return 1; }
    wait "$lpid"; rc=$?
    [[ "$rc" == "$want" ]] && grep -qE '^/PID [0-9]+ /T /F$' "$FAKE_TASKKILL"
}
check "SIGTERM kills the registered Windows process tree and exits 143" cancel_case TERM 143
# (SIGINT is not testable here: a backgrounded non-interactive shell ignores it.)
check "SIGHUP kills the registered Windows process tree and exits 129" cancel_case HUP 129
check "a finished run leaves no process-tree PID file behind" \
    bash -c '"$0" --log "$1/pid.log" bat build.bat >/dev/null; ! ls "$1"/.native-root-*.pid >/dev/null 2>&1' "$launcher" "$scratch"

exit "$failures"
