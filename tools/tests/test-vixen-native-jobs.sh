#!/usr/bin/env bash
# Shell test for tools/vixen-native.sh `jobs`: queued native width comes from the live grant file, while
# direct runs use Windows logical cores. The stale/oversize claim fixture proves no second shell admission
# can collapse a queued grant.
set -uo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
script="$here/../vixen-native.sh"
work="$(mktemp -d "${TMPDIR:-$here}/vixen-native-jobs.XXXXXX")"
trap 'rm -rf "$work"' EXIT
while IFS= read -r _v; do unset "$_v"; done < <(compgen -e | grep -E '^(UT_QUEUE_|NW_|VIXEN_MAX_BUILD_JOBS$|VIXEN_BUILD_MB_PER_JOB$|VIXEN_NATIVE_AVAILABLE_MB$)' || true)
mkdir -p "$work/bin" "$work/state"
export UT_QUEUE_STATE_DIR="$work/state"
printf '#!/usr/bin/env bash\nprintf "%%s\\r\\n" "${FAKE_WINDOWS_CORES:-16}"\n' > "$work/bin/cmd.exe"
chmod +x "$work/bin/cmd.exe"
grant="$work/grant"
printf 'job pid=1 granted=1 grant-cpus=4 start-grant=1\n' >"$grant"
Q="UT_QUEUE_HOLDER_PID=$$"
G="UT_QUEUE_GRANT_FILE=$grant"
fails=0

# jobs EXPECTED NAME [ENV=VALUE ...]: runs `jobs` with the given variables on top of the scrubbed environment.
jobs_is() {
    local expected="$1" name="$2"; shift 2
    local out got
    out="$(env PATH="$work/bin:$PATH" "$@" bash "$script" jobs 2>&1)"
    got="$(printf '%s\n' "$out" | tail -n 1)"
    if [[ "$got" == "$expected" ]]; then echo "ok   $name -> $got"; else echo "FAIL $name: expected $expected, got '$got'"; printf '%s\n' "$out"; fails=$((fails + 1)); fi
    LAST_OUT="$out"
}
line_has() {
    if grep -Fq -- "$2" <<<"$LAST_OUT"; then echo "ok   $1 log has '$2'"; else echo "FAIL $1: log lacks '$2'"; printf '%s\n' "$LAST_OUT"; fails=$((fails + 1)); fi
}
line_lacks() {
    if ! grep -Fq -- "$2" <<<"$LAST_OUT"; then echo "ok   $1 log omits '$2'"; else echo "FAIL $1: log contains '$2'"; printf '%s\n' "$LAST_OUT"; fails=$((fails + 1)); fi
}

# The 11-15 fixture: a four-CPU queue grant must remain four even when a stale/oversize claim says 37,661 MB.
printf '37661\npid=424242\nstart_ticks=1\nlabel=other:native-build\n' >"$work/state/undertow-width-claim.424242"
jobs_is 4 "queue grant wins over a stale oversize claim" "$Q" "$G" UT_QUEUE_CREDITS=1 UT_QUEUE_CPUS=2 VIXEN_NATIVE_AVAILABLE_MB=12469
line_has "queue grant" "source=queue grant-cpus=4"
line_lacks "queue claim is not a second admission" "other_claims_mb"

# A live top-up is visible at the next build step, independent of the Windows snapshot.
printf 'job pid=1 granted=2 grant-cpus=8 start-grant=1\n' >"$grant"
jobs_is 8 "the next step follows a queue top-up" "$Q" "$G" VIXEN_NATIVE_AVAILABLE_MB=100

# Manual grants are still queue grants and are read from the same atomically rewritten row.
printf 'job pid=1 granted=3 grant-cpus=12 grant-manual=1\n' >"$grant"
jobs_is 12 "a manual queue grant is used" "$Q" "$G" VIXEN_NATIVE_AVAILABLE_MB=100
line_has "manual" "source=manual queue grant"

# Direct runs have no queue grant and use the Windows logical-core width; Windows memory is not a second shell admission.
jobs_is 16 "direct width is Windows logical cores" FAKE_WINDOWS_CORES=16 VIXEN_NATIVE_AVAILABLE_MB=100
line_has "direct" "source=direct Windows logical cores=16"

# An explicit direct caller cap remains an explicit caller choice outside queue admission.
jobs_is 3 "explicit direct job cap wins" VIXEN_MAX_BUILD_JOBS=3 FAKE_WINDOWS_CORES=16
line_has "explicit" "explicit VIXEN_MAX_BUILD_JOBS"
exit "$fails"
