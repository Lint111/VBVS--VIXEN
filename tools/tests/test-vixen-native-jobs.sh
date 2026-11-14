#!/usr/bin/env bash
# Shell test for the native build job-count derivation (tools/vixen-native.sh `jobs`): fake UT_QUEUE_*
# values, no explicit VIXEN_MAX_BUILD_JOBS, and a fake cmd.exe so no Windows is needed.
set -uo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
script="$here/../vixen-native.sh"
work="$(mktemp -d "${TMPDIR:-$here}/vixen-native-jobs.XXXXXX")"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/bin"
printf '#!/usr/bin/env bash\nprintf "%%s\\r\\n" "${FAKE_WINDOWS_CORES:-16}"\n' > "$work/bin/cmd.exe"
chmod +x "$work/bin/cmd.exe"
fails=0

# jobs EXPECTED NAME [ENV=VALUE ...]: runs `jobs` with a clean UT_QUEUE_* / VIXEN_* environment plus the given variables.
jobs_is() {
    local expected="$1" name="$2"; shift 2
    local out got
    out="$(env -u VIXEN_MAX_BUILD_JOBS -u UT_QUEUE_CREDITS -u UT_QUEUE_CPUS -u UT_QUEUE_MEMORY_MB \
        -u VIXEN_BUILD_MB_PER_JOB -u VIXEN_NATIVE_AVAILABLE_MB PATH="$work/bin:$PATH" "$@" \
        bash "$script" jobs 2>&1)"
    got="$(printf '%s\n' "$out" | tail -n 1)"
    if [[ "$got" == "$expected" ]]; then echo "ok   $name -> $got"; else echo "FAIL $name: expected $expected, got '$got'"; printf '%s\n' "$out"; fails=$((fails + 1)); fi
    LAST_OUT="$out"
}
line_has() {
    if grep -Fq -- "$2" <<<"$LAST_OUT"; then echo "ok   $1 log has '$2'"; else echo "FAIL $1: log lacks '$2'"; printf '%s\n' "$LAST_OUT"; fails=$((fails + 1)); fi
}

# In the queue: granted CPUs, capped by memory / per-job figure (default 900 MB).
jobs_is 7 "queue memory-capped (30 cpus, 7168 MB)" UT_QUEUE_CREDITS=15 UT_QUEUE_CPUS=30 UT_QUEUE_MEMORY_MB=7168
line_has "queue memory-capped" "source=queue cpus=30 memory_mb=7168 per_job_mb=900"
jobs_is 6 "queue cpu-capped (6 cpus, 65536 MB)" UT_QUEUE_CREDITS=3 UT_QUEUE_CPUS=6 UT_QUEUE_MEMORY_MB=65536
jobs_is 1 "queue memory below one job floors at 1" UT_QUEUE_CREDITS=3 UT_QUEUE_CPUS=6 UT_QUEUE_MEMORY_MB=100
jobs_is 12 "queue without a memory figure uses the CPUs" UT_QUEUE_CREDITS=6 UT_QUEUE_CPUS=12
line_has "queue without memory" "memory_mb=unknown"
jobs_is 10 "per-job figure override (4000 MB / 400)" UT_QUEUE_CREDITS=15 UT_QUEUE_CPUS=30 UT_QUEUE_MEMORY_MB=4000 VIXEN_BUILD_MB_PER_JOB=400
jobs_is 16 "queue credits without CPUs fall back to the Windows core count" UT_QUEUE_CREDITS=8 UT_QUEUE_MEMORY_MB=65536
# Explicit value wins over the queue grant.
jobs_is 3 "explicit VIXEN_MAX_BUILD_JOBS wins" VIXEN_MAX_BUILD_JOBS=3 UT_QUEUE_CREDITS=15 UT_QUEUE_CPUS=30 UT_QUEUE_MEMORY_MB=7168
line_has "explicit" "explicit VIXEN_MAX_BUILD_JOBS"
# Outside the queue: logical cores, capped by available memory.
jobs_is 16 "direct, plenty of memory (16 cores)" VIXEN_NATIVE_AVAILABLE_MB=60000
jobs_is 4 "direct, 3600 MB available" VIXEN_NATIVE_AVAILABLE_MB=3600 FAKE_WINDOWS_CORES=32
line_has "direct" "source=direct cpus=32 memory_mb=3600 per_job_mb=900"
exit "$fails"
