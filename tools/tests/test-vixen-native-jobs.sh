#!/usr/bin/env bash
# Shell test for the native build job-count derivation (tools/vixen-native.sh `jobs`): fixture memory and
# queue state, no explicit VIXEN_MAX_BUILD_JOBS, and a fake cmd.exe so no Windows is needed.
set -uo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
script="$here/../vixen-native.sh"
work="$(mktemp -d "${TMPDIR:-$here}/vixen-native-jobs.XXXXXX")"
trap 'rm -rf "$work"' EXIT
# Inside a queue job the UT_QUEUE_* / VIXEN_* variables describe the live host: drop them all and size from fixtures.
while IFS= read -r _v; do unset "$_v"; done < <(compgen -e | grep -E '^(UT_QUEUE_|NW_|VIXEN_MAX_BUILD_JOBS$|VIXEN_BUILD_MB_PER_JOB$|VIXEN_NATIVE_AVAILABLE_MB$)' || true)
mkdir -p "$work/bin" "$work/state"
export UT_QUEUE_STATE_DIR="$work/state"
printf '#!/usr/bin/env bash\nprintf "%%s\\r\\n" "${FAKE_WINDOWS_CORES:-16}"\n' > "$work/bin/cmd.exe"
chmod +x "$work/bin/cmd.exe"
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

# R847.4: the width is node width (Windows logical cores) capped by memory, jobs = max(1, min(cores, (available - other
# builds' young claims - 1024 headroom) / per_job_mb)); the queue grant (UT_QUEUE_CREDITS/CPUS/MEMORY_MB) never sets it. A
# queue-admitted build (UT_QUEUE_HOLDER_PID set) keeps a 4-job floor while memory covers 4 * per_job. Memory and the state dir
# are fixtures, so nothing here depends on the live host or the live queue.
Q="UT_QUEUE_HOLDER_PID=$$"
jobs_is 16 "queue grant is ignored: 6 granted CPUs still size at node width" "$Q" UT_QUEUE_CREDITS=3 UT_QUEUE_CPUS=6 UT_QUEUE_MEMORY_MB=65536 VIXEN_NATIVE_AVAILABLE_MB=60000
line_has "queue" "source=queue cores=16 windows_available_mb=60000 other_claims_mb=0 other_holders=0 per_job_mb=900"
jobs_is 6 "memory caps the width: (7168 - 1024) / 900" "$Q" VIXEN_NATIVE_AVAILABLE_MB=7168
jobs_is 1 "memory below one job floors at 1" "$Q" VIXEN_NATIVE_AVAILABLE_MB=100
jobs_is 4 "a queue-admitted build keeps the 4-job floor while memory covers 4 * per_job" "$Q" VIXEN_NATIVE_AVAILABLE_MB=3700
jobs_is 2 "the floor yields below 4 * per_job (3500 -> 2 by memory)" "$Q" VIXEN_NATIVE_AVAILABLE_MB=3500
jobs_is 7 "per-job figure override (4000 MB / 400): (4000 - 1024) / 400" "$Q" VIXEN_NATIVE_AVAILABLE_MB=4000 VIXEN_BUILD_MB_PER_JOB=400
line_has "per-job" "per_job_mb=400"
# Explicit value wins over the node rule.
jobs_is 3 "explicit VIXEN_MAX_BUILD_JOBS wins" VIXEN_MAX_BUILD_JOBS=3 "$Q" VIXEN_NATIVE_AVAILABLE_MB=7168
line_has "explicit" "explicit VIXEN_MAX_BUILD_JOBS"
# Outside the queue: logical cores, capped by available memory, no admission floor.
jobs_is 16 "direct, plenty of memory (16 cores)" VIXEN_NATIVE_AVAILABLE_MB=60000
jobs_is 2 "direct, 3600 MB available (no floor): (3600 - 1024) / 900" VIXEN_NATIVE_AVAILABLE_MB=3600 FAKE_WINDOWS_CORES=32
line_has "direct" "source=direct cores=32 windows_available_mb=3600 other_claims_mb=0 other_holders=0 per_job_mb=900"
exit "$fails"
