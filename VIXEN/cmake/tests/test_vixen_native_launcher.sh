#!/usr/bin/env bash
# tools/vixen-native.sh against a fake System32 (cmd.exe stub): command discovery, Windows path
# quoting, argument pass-through and exit codes.
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
pwd > "$FAKE_CWD"
echo "fake cmd output"
exit "${FAKE_RC:-0}"
STUB
chmod +x "$fake/cmd.exe"
export FAKE_ARGV="$scratch/argv.txt" FAKE_CWD="$scratch/cwd.txt"
export VIXEN_NATIVE_SYSTEM32="$fake"
# A PATH with no Windows entries, as in an SSH shell.
export PATH="/usr/bin:/bin"

lane_repo="$scratch/lane-a"
mkdir -p "$lane_repo/tools"
cp "$repo_root/tools/vixen-native.sh" "$lane_repo/tools/vixen-native.sh"
printf '@echo off\n' > "$lane_repo/build.bat"
launcher="$lane_repo/tools/vixen-native.sh"

check "cmd.exe found through the System32 override when PATH lacks it" \
    test "$("$launcher" cmd)" = "$fake/cmd.exe"
check "build passes the action, preset and target to build.bat with a Windows batch path" \
    bash -c '"$0" build all vixen-ninja VixenApp >/dev/null; mapfile -t a < "$FAKE_ARGV"; test "${a[0]}" = /c && [[ "${a[1]}" == *build.bat ]] && [[ "${a[1]}" == *\\* ]] && test "${a[2]}" = all && test "${a[3]}" = vixen-ninja && test "${a[4]}" = VixenApp' "$launcher"
check "an argument with spaces stays one argument" \
    bash -c '"$0" bat build.bat "two words" >/dev/null; mapfile -t a < "$FAKE_ARGV"; test "${#a[@]}" = 3 && test "${a[2]}" = "two words"' "$launcher"
check "bat refuses a path that leaves the worktree" \
    bash -c '! "$0" bat ../elsewhere.bat >/dev/null 2>&1' "$launcher"
check "build exit code is the batch file's exit code" \
    bash -c 'FAKE_RC=7 "$0" build >/dev/null; test $? = 7' "$launcher"
check "build output is kept in the log" \
    bash -c 'log="$1/build.log"; "$0" --log "$log" build >/dev/null; grep -q "fake cmd output" "$log"' "$launcher" "$scratch"

exit "$failures"
