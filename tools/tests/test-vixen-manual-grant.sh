#!/usr/bin/env bash
# sn-qregrant1: a manual queue grant in UT_QUEUE_GRANT_FILE sets the native build width at the start of each step; an automatic grant does not.
set -uo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
work="$(mktemp -d "${TMPDIR:-$here}/vixen-manual-grant.XXXXXX")"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/bin"
printf '#!/usr/bin/env bash\nprintf "%%s\\r\\n" "${FAKE_WINDOWS_CORES:-16}"\n' > "$work/bin/cmd.exe"
chmod +x "$work/bin/cmd.exe"
fails=0
check() { if [[ "$1" == "$2" ]]; then echo "ok   $3 -> $1"; else echo "FAIL $3: expected '$2', got '$1'"; fails=$((fails + 1)); fi; }
grant="$work/grant"
source "$here/../node-width.sh"
jobs_now() { env -u VIXEN_MAX_BUILD_JOBS UT_QUEUE_GRANT_FILE="$grant" PATH="$work/bin:$PATH" VIXEN_NATIVE_AVAILABLE_MB=60000 bash "$here/../vixen-native.sh" jobs 2>&1; }

printf 'job pid=1 granted=1 grant-cpus=2 reserved-others-mb=0\n' >"$grant"
UT_QUEUE_GRANT_FILE="$grant" nw_manual_grant_cpus >/dev/null; check "$?" 1 "automatic grant row is not a manual grant"
printf 'job pid=1 granted=6 grant-cpus=12 grant-manual=1 grant-from=1 grant-to=6\n' >"$grant"
check "$(UT_QUEUE_GRANT_FILE="$grant" nw_manual_grant_cpus)" 12 "manual grant cpus"
out="$(jobs_now)"; check "$(tail -n 1 <<<"$out")" 12 "jobs follows the manual grant"
grep -Fq "source=manual queue grant" <<<"$out" && echo "ok   log names the manual grant" || { echo "FAIL log lacks the manual grant source"; fails=$((fails + 1)); }
printf 'job pid=1 granted=2 grant-cpus=4 grant-manual=1\n' >"$grant"
check "$(tail -n 1 <<<"$(jobs_now)")" 4 "the next step re-reads a changed grant"
printf 'job pid=1 granted=2 grant-cpus=4\n' >"$grant"
check "$(tail -n 1 <<<"$(jobs_now)")" 16 "after a reset the node width rule applies again"
check "$(UT_QUEUE_GRANT_FILE=/nonexistent nw_manual_grant_cpus; echo "rc=$?")" "rc=1" "a missing grant file is not a manual grant"
exit "$fails"
