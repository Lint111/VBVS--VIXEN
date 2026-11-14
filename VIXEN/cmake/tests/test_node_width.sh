#!/usr/bin/env bash
# tools/node-width.sh (R847.4): node width capped by memory, floor 1, other holders' uncommitted memory
# and claims off the budget, and two heavy steps starting back to back not double-booking memory.
# Usage: test_node_width.sh <repo-root>
set -uo pipefail
repo_root="${1:?repo root}"
# Sizing inputs come from this test's fixtures only: a test that runs inside a queue job inherits the live job's
# UT_QUEUE_* variables (holder pid, grant file, other holders' reserved MB, ...) and must not read them.
while IFS= read -r _v; do unset "$_v"; done < <(compgen -e | grep -E '^(UT_QUEUE_|NW_|VIXEN_MAX_BUILD_JOBS$|VIXEN_BUILD_MB_PER_JOB$|VIXEN_NATIVE_AVAILABLE_MB$)' || true)
state="$(mktemp -d)"
sleep 300 & a=$!
sleep 300 & b=$!
trap 'kill "$a" "$b" 2>/dev/null; rm -rf "$state"' EXIT
export UT_QUEUE_STATE_DIR="$state"
# shellcheck source=../../../tools/node-width.sh
source "$repo_root/tools/node-width.sh"
failures=0
expect() { local want="$1" got="$2" name="$3"; if [[ "$got" == "$want" ]]; then echo "PASS $name"; else echo "FAIL $name: want $want got $got"; failures=$((failures + 1)); fi; }

expect 16 "$(nw_jobs 16 45149 1500)" "node width when memory allows"
expect 8 "$(nw_jobs 32 17408 2048)" "memory caps the width"
expect 1 "$(nw_jobs 32 500 10000)" "floor 1 when nothing fits"
expect 1 "$(nw_jobs 32 1024 900)" "floor 1 at exactly the headroom"

ticks() { sed 's/.*) //' "/proc/$1/stat" | awk '{ print $20 }'; }
printf '9000\npid=%s\ncurrent=1000\nstart_ticks=%s\n' "$a" "$(ticks "$a")" > "$state/undertow-box-memory-reservation.$a"
expect "8000 1" "$(nw_other_uncommitted_mb)" "reserved minus current is uncommitted"
expect "0 0" "$(UT_QUEUE_HOLDER_PID=$a nw_other_uncommitted_mb)" "this holder's own reservation is skipped"
printf '9000\npid=%s\ncurrent=1000\nstart_ticks=1\n' "$a" > "$state/undertow-box-memory-reservation.$a"
expect "0 0" "$(nw_other_uncommitted_mb)" "a recycled pid (start ticks differ) is ignored"
rm -f "$state"/undertow-box-memory-reservation.*
expect "700 0" "$(UT_QUEUE_RESERVED_OTHERS_MB=700 nw_other_uncommitted_mb)" "the queue's exported unspent reservations are a floor"
expect "0 0" "$(nw_other_uncommitted_mb)" "no queue export and no files: nothing is uncommitted"

# Two steps back to back, both still seeing 20000 MB available (the first has not grown yet).
size_and_claim() {
    local holder="$1" jobs other
    other="$(UT_QUEUE_HOLDER_PID=$holder nw_other_uncommitted_mb | cut -d' ' -f1)"
    jobs="$(nw_jobs 32 $((20000 - other)) 1000)"
    UT_QUEUE_HOLDER_PID=$holder nw_claim_end $((jobs * 1000)) "test:$holder"
    echo "$jobs"
}
first="$(UT_QUEUE_HOLDER_PID=$a nw_claim_begin; size_and_claim "$a")"
expect 18 "$first" "first heavy step sizes to the node"
expect "18000" "$(head -1 "$state/undertow-width-claim.$a")" "first step publishes its claim"
second="$(size_and_claim "$b")"
expect 1 "$second" "second heavy step sizes to what is left, not the whole node"
UT_QUEUE_HOLDER_PID=$a nw_claim_release
rm -f "$state/undertow-width-claim.$b"
expect 18 "$(size_and_claim "$b")" "a later step sees the freed node"

# Windows-side sizing (sn-vxjobs1): reservations are ignored, young claims count, the floor holds.
rm -f "$state"/undertow-width-claim.*
printf '30000\npid=%s\ncurrent=1000\nstart_ticks=%s\n' "$a" "$(ticks "$a")" > "$state/undertow-box-memory-reservation.$a"
expect "0 0" "$(UT_QUEUE_RESERVED_OTHERS_MB=45589 nw_other_claims_mb)" "claims sizer ignores reservations and the queue's reserved-others export"
rm -f "$state"/undertow-box-memory-reservation.*
printf '3600\npid=%s\nstart_ticks=%s\n' "$a" "$(ticks "$a")" > "$state/undertow-width-claim.$a"
expect "3600 1" "$(nw_other_claims_mb)" "another live build's claim counts"
expect "0 0" "$(UT_QUEUE_HOLDER_PID=$a nw_other_claims_mb)" "own claim is skipped"
touch -d "10 minutes ago" "$state/undertow-width-claim.$a"
expect "0 0" "$(nw_other_claims_mb)" "a claim older than the ramp window is already in the counter"
rm -f "$state/undertow-width-claim.$a"
expect 4 "$(nw_floor_jobs 1 32 4000 900)" "floor lifts 1 to 4 when memory covers 4 jobs"
expect 1 "$(nw_floor_jobs 1 32 3500 900)" "floor yields when memory is below 4 * per_job"
expect 6 "$(nw_floor_jobs 6 32 9000 900)" "floor never lowers a wider answer"
expect 2 "$(nw_floor_jobs 1 2 5000 900)" "floor is capped at cores"
exit "$failures"
