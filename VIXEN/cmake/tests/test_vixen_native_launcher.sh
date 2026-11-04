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
failures=0

check() {
    local name="$1"; shift
    if "$@"; then echo "PASS $name"; else echo "FAIL $name"; failures=$((failures + 1)); fi
}

fake="$scratch/fake/Windows/System32"
mkdir -p "$fake"
cat > "$fake/cmd.exe" <<'STUB'
#!/usr/bin/env bash
printf '%s\n' "$@" > "$FAKE_ARGV"
echo "$*" >> "$FAKE_CALLS"
pwd > "$FAKE_CWD"
printf '%s\n' "${WSLENV-}" > "$FAKE_WSLENV"
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
export FAKE_WSLENV="$scratch/wslenv.txt"
export VIXEN_NATIVE_SYSTEM32="$fake"
# A PATH with no Windows entries, as in an SSH shell.
export PATH="/usr/bin:/bin"

lane_repo="$scratch/lane-a"
mkdir -p "$lane_repo/tools" "$lane_repo/VIXEN"
cp "$repo_root/tools/vixen-native.sh" "$lane_repo/tools/vixen-native.sh"
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
check "an argument with spaces stays one argument" \
    bash -c '"$0" bat build.bat "two words" >/dev/null; mapfile -t a < "$FAKE_ARGV"; test "${#a[@]}" = 3 && test "${a[2]}" = "two words"' "$launcher"
check "YEROKET_ROOT is forwarded into the Windows process as a path" \
    bash -c 'YEROKET_ROOT=/home/x/Yeroket-Fantasy "$0" bat build.bat >/dev/null; tr ":" "\n" < "$FAKE_WSLENV" | grep -qx "YEROKET_ROOT/p"' "$launcher"
check "bat refuses a path that leaves the worktree" \
    bash -c '! "$0" bat ../elsewhere.bat >/dev/null 2>&1' "$launcher"
check "build exit code is the batch file's exit code" \
    bash -c 'FAKE_RC=7 "$0" build >/dev/null; test $? = 7' "$launcher"
check "build output is kept in the log" \
    bash -c 'log="$1/build.log"; "$0" --log "$log" build >/dev/null; grep -q "fake cmd output" "$log"' "$launcher" "$scratch"
check "a provisioning stop (exit 20) stops the build before build.bat runs" \
    bash -c 'rm -rf "$1/lane-a/VIXEN/.win-native-toolchain"; : > "$FAKE_CALLS"; FAKE_RC=20 "$0" build all >/dev/null; rc=$?; test $rc = 20 && test "$(wc -l < "$FAKE_CALLS")" = 1 && grep -q "build.bat provision" "$FAKE_CALLS"' "$launcher" "$native"

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
