#!/usr/bin/env bash
set -euo pipefail

# Each lighting state owns one real queue job, including its fresh-process repeat.
if [[ $# -lt 2 || $# -gt 3 ]]; then
    echo "Usage: tools/run-lightunify-captures.sh <build-dir> <output-dir> [first-state]" >&2
    exit 2
fi
build_dir="$(cd -- "$1" && pwd -P)"
mkdir -p -- "$2"
output_dir="$(cd -- "$2" && pwd -P)"
started=false
for state in midday late-afternoon overcast night-service; do
    if [[ "$state" == "${3:-midday}" ]]; then started=true; fi
    if [[ "$started" != true ]]; then continue; fi
    mkdir -p -- "$output_dir/$state"
    # The queue creates a retained per-run child below VIXEN_CAPTURE_ROOT; the look-dev captures land there.
    capture_base="$output_dir/$state/capture-root"
    mkdir -p -- "$capture_base"
    env UT_STALL_IDLE_HARD_S=1800 VIXEN_CAPTURE_ROOT="$capture_base" VIXEN_LOOKDEV_CAPTURE_STATE="$state" \
        VIXEN_LOOKDEV_READBACK=1 VIXEN_PERF_CSV="$output_dir/$state/gpu-timing.csv" \
        bash /home/liory/.local/bin/with-test-lock.sh --agent lightunify --resource gpu \
        --label "lightunify:$(basename -- "$output_dir"):$state" -- \
        ctest --test-dir "$build_dir" -R '^Application.LookdevCapture\.' --output-on-failure \
        >"$output_dir/$state/run.log" 2>&1
    run_root="$(find "$capture_base" -mindepth 1 -maxdepth 1 -type d -printf '%T@ %p\n' | sort -n | tail -n 1 | cut -d' ' -f2-)"
    if [[ -z "$run_root" || ! -d "$run_root/lookdev/$state" ]]; then
        echo "No look-dev captures for $state under $capture_base (expected <queue run child>/lookdev/$state); see $output_dir/$state/run.log" >&2
        exit 1
    fi
    cp -- "$run_root/lookdev/$state/"* "$output_dir/$state/"
    echo "$state captures and repeated-process pixel gate passed"
done
if [[ "$started" != true ]]; then echo "Unknown first state: $3" >&2; exit 2; fi
