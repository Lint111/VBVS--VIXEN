#!/usr/bin/env bash
# One measured ctest run of a label with a given GPU slot count, plus a hash of every PNG it wrote.
#
# Usage: tools/measure-gpu-concurrency.sh BUILD_DIR OUT_DIR SLOTS LABEL [ctest args...]
#   SLOTS   number of `gpu` resource slots (writes OUT_DIR/gpu-slots-N.json and passes it as
#           --resource-spec-file), or `lock` to run the build as configured (no spec override).
#   LABEL   ctest -L regex, e.g. RenderGraph.
# Writes OUT_DIR: ctest.log, junit.xml, summary.txt (wall seconds, ctest -j, per-label time) and
# png-sha256.txt (sorted "sha256  relative-path" of every PNG created or changed under BUILD_DIR and UT_QUEUE_CAPTURE_ROOT during the run).
# Run it inside a queue GPU job; the queue grant is the ctest width.
set -u
build="$1"; out="$2"; slots="$3"; label="$4"; shift 4
jobs="${UT_QUEUE_GRANTED_CPUS:-$(nproc)}"
mkdir -p "$out"
marker="$out/.start-marker"; : > "$marker"
spec=()
if [[ "$slots" != lock ]]; then
  printf '{"version":{"major":1,"minor":0},"local":[{"gpu":[{"id":"0","slots":%s}]}]}\n' "$slots" > "$out/gpu-slots-$slots.json"
  spec=(--resource-spec-file "$out/gpu-slots-$slots.json")
fi
started=$SECONDS
ctest --test-dir "$build" -L "$label" -j "$jobs" ${spec[@]+"${spec[@]}"} --output-junit "$out/junit.xml" \
  --output-log "$out/ctest.log" "$@" > "$out/ctest-stdout.log" 2>&1
rc=$?
wall=$((SECONDS - started))
# The queue redirects producer captures to UT_QUEUE_CAPTURE_ROOT; tests also write under the build tree.
# Paths are made relative to their root so two runs (with different capture roots) diff directly.
: > "$out/png-sha256.txt"
for root in "$build" "${UT_QUEUE_CAPTURE_ROOT:-}"; do
  [[ -n "$root" && -d "$root" ]] || continue
  (cd "$root" && find . -name '*.png' -newer "$marker" -not -path '*/_deps/*' -print0 | sort -z | xargs -0 -r sha256sum) >> "$out/png-sha256.txt"
done
sort -k2 -o "$out/png-sha256.txt" "$out/png-sha256.txt"
{
  echo "slots=$slots label=$label ctest_j=$jobs rc=$rc wall_s=$wall"
  echo "png_files=$(wc -l < "$out/png-sha256.txt")"
  grep -A8 "Label Time Summary" "$out/ctest-stdout.log" | head -12
  grep -E "tests passed|Total Test time" "$out/ctest-stdout.log"
} > "$out/summary.txt"
rm -f "$marker"
exit 0
