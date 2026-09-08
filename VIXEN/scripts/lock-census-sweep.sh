#!/usr/bin/env bash
# Lock-census baseline sweep — lock-free federation phase 0 (design §9 measurements 1-5).
#
# Runs a VIXEN_LOCK_CENSUS=ON build of the app for a fixed frame count across the graph executors
# and worker counts, collecting per-frame CSVs and the "[LockCensus]" teardown summaries into one
# directory. Measurement 5 (1/2/N byte-identity) is NOT asserted here: pair each leg with the
# existing capture/golden tooling (VIXEN_EDITOR_CAPTURE_DIR / tools/bench/compare_parity.py) when a
# frame-hash comparison is wanted; this script only makes the timing/acquisition legs comparable.
#
# usage: lock-census-sweep.sh <VIXEN binary from a -DVIXEN_LOCK_CENSUS=ON build> [out-dir] [frames]
#   env: VIXEN_CENSUS_WORKERS="1 2 4" (worker counts for the lowered legs)
#        any VIXEN_* scene/knob variables are passed through to every leg
set -euo pipefail

BIN="${1:?usage: $0 <VIXEN binary> [out-dir] [frames]}"
OUT="${2:-lock-census-out}"
FRAMES="${3:-300}"
WORKERS="${VIXEN_CENSUS_WORKERS:-1 2 4}"

mkdir -p "$OUT"
echo "binary=$BIN out=$OUT frames=$FRAMES workers=[$WORKERS]"

run_leg() {
    local name="$1"; shift
    echo "== leg $name"
    # The graph writes the CSV; stderr carries the [LockCensus] summary at graph teardown.
    if ! env "$@" VIXEN_EXIT_AFTER_FRAMES="$FRAMES" VIXEN_LOCK_CENSUS_CSV="$OUT/$name.csv" \
            "$BIN" > "$OUT/$name.log" 2>&1; then
        echo "   leg $name exited nonzero (see $OUT/$name.log)"
    fi
    grep '\[LockCensus\]' "$OUT/$name.log" > "$OUT/$name.summary" || true
    if [[ ! -s "$OUT/$name.summary" ]]; then
        echo "   no [LockCensus] summary — is $BIN a VIXEN_LOCK_CENSUS=ON build?"
    else
        head -1 "$OUT/$name.summary"
    fi
}

run_leg sequential VIXEN_GRAPH_LOWERED=0
for w in $WORKERS; do
    run_leg "lowered-w$w" VIXEN_GRAPH_LOWERED=1 VIXEN_GRAPH_WORKERS="$w"
done

echo "== summaries in $OUT/*.summary, per-frame CSVs in $OUT/*.csv"
