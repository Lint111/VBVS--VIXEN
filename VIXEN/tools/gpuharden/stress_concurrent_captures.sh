#!/usr/bin/env bash
# R516 gpuharden: N concurrent headless correctness captures; records device-loss rate, wall time and
# memory peaks per N. Run it INSIDE the gpu queue lock:
#   with-test-lock.sh --resource gpu --label gpuharden-stress \
#     bash -lc 'VK_ICD_FILENAMES=$HOME/.cache/vixen/wsl-vulkan/dzn_icd.json tools/gpuharden/stress_concurrent_captures.sh BUILD_DIR OUT_DIR "1 2 3 4" REPS'
# Each process renders the starfield capture to its own prefix; PNGs are compared byte-for-byte with
# the first N=1 capture, so a "completed" run is also a correct run.
set -u
BUILD=${1:?build dir}; OUT=${2:?output dir}; NS=${3:-"1 2 3 4"}; REPS=${4:-2}
APPDIR="$BUILD/application/main"
# WORKLOAD=starfield (light, ~6 s, default) or cornell (heavy production graph, ~4 min solo on Dozen).
WORKLOAD=${WORKLOAD:-starfield}; EXTRA_ENV=""
case "$WORKLOAD" in
  starfield) BIN="./test_headless_starfield_graph"; FILTER="HeadlessStarfieldGraph.CaptureSingleRun"
             PREFIX_ENV=VIXEN_HEADLESS_STARFIELD_CAPTURE_PREFIX; PNG=-on.png ;;
  cornell)   EXTRA_ENV="VIXEN_DDGI_CORNELL_VIRTUAL_DEMO=1 VIXEN_TEST_CELSHADE_LAMBERT_GGX=1"; BIN="./test_headless_cornell_graph"; FILTER="HeadlessCornellGraph.*"
             PREFIX_ENV=VIXEN_HEADLESS_CORNELL_CAPTURE_PREFIX; PNG=-a.png ;;
  *) echo "unknown WORKLOAD=$WORKLOAD" >&2; exit 2 ;;
esac
mkdir -p "$OUT"
TSV="$OUT/results.tsv"
printf 'N\trep\tproc\texit\twall_s\tmaxrss_mb\tvram_peak_mb\tgpu_adapter_delta_mb\tdevice_loss\trecovered\tpng_match\n' > "$TSV"
REF=""

PS=/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe
# Adapter-wide GPU memory (dedicated+shared) from the Windows counters, sampled once a second. Dozen
# does not expose VK_EXT_memory_budget, so this is the only view of GPU memory peaks from WSL.
start_sampler() { # file
  if [ -x "$PS" ]; then
    "$PS" -NoProfile -Command "Get-Counter '\\GPU Adapter Memory(*)\\Dedicated Usage','\\GPU Adapter Memory(*)\\Shared Usage' -Continuous -SampleInterval 1 | ForEach-Object { (\$_.CounterSamples | ForEach-Object { [string][int64]\$_.CookedValue }) -join ',' }" > "$1" 2>/dev/null &
    SAMPLER=$!; sleep 5
  else SAMPLER=""; fi
}
stop_sampler() { [ -n "${SAMPLER:-}" ] && kill "$SAMPLER" 2>/dev/null; wait "$SAMPLER" 2>/dev/null; SAMPLER=""; }
gpu_delta_mb() { python3 - "$1" <<'PY'
import sys
rows=[]
for l in open(sys.argv[1], errors="ignore"):
    l=l.strip().replace("\r","")
    if not l: continue
    try: rows.append(sum(int(x) for x in l.split(",")))
    except ValueError: pass
print((max(rows)-rows[0])//(1<<20) if len(rows)>1 else "?")
PY
}

run_one() { # n rep idx
  local n=$1 rep=$2 i=$3 tag="n$1-r$2-p$3"
  local prefix="$OUT/$tag"
  ( cd "$APPDIR" && VIXEN_MEMBUDGET_LOG=1 env "$PREFIX_ENV=$prefix" $EXTRA_ENV \
      /usr/bin/time -f "TIME wall=%e maxrss=%M" "$BIN" --gtest_filter="$FILTER" ) > "$OUT/$tag.log" 2>&1
  echo $? > "$OUT/$tag.exit"
}

# Warm-up: one uncounted capture primes the shader/pipeline caches (a cold start is ~3x slower and
# would skew N=1) and fixes the reference PNG.
run_one 0 0 0
echo "warm-up exit=$(cat "$OUT/n0-r0-p0.exit") wall=$(sed -n 's/^TIME wall=\([0-9.]*\).*/\1/p' "$OUT/n0-r0-p0.log" | tail -1)" >&2
[ -f "$OUT/n0-r0-p0$PNG" ] && REF=$(sha256sum "$OUT/n0-r0-p0$PNG" | cut -d' ' -f1)

for n in $NS; do
  for rep in $(seq 1 "$REPS"); do
    pids=()
    SAMPLE="$OUT/gpumem-n$n-r$rep.csv"; start_sampler "$SAMPLE"
    for i in $(seq 1 "$n"); do run_one "$n" "$rep" "$i" & pids+=($!); done
    wait "${pids[@]}"
    sleep 1; stop_sampler
    GDELTA=$(gpu_delta_mb "$SAMPLE")
    for i in $(seq 1 "$n"); do
      tag="n$n-r$rep-p$i"; log="$OUT/$tag.log"
      ex=$(cat "$OUT/$tag.exit")
      wall=$(sed -n 's/^TIME wall=\([0-9.]*\) maxrss=.*/\1/p' "$log" | tail -1)
      rss=$(sed -n 's/^TIME wall=[0-9.]* maxrss=\([0-9]*\)/\1/p' "$log" | tail -1)
      vram=$(sed -n 's/.*peak live device-local usage \([0-9]*\) MB.*/\1/p' "$log" | tail -1)
      loss=$(grep -cE "D3D12: Removing Device|VK_ERROR_DEVICE_LOST|DEVICE-LOSS RECOVERY" "$log")
      rec=$(grep -c "RECOVERY COMPLETE" "$log")
      match=NA
      if [ -f "$OUT/$tag$PNG" ]; then
        h=$(sha256sum "$OUT/$tag$PNG" | cut -d' ' -f1)
        if [ -z "$REF" ]; then REF=$h; fi
        [ "$h" = "$REF" ] && match=yes || match=NO
      fi
      printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$n" "$rep" "$i" "$ex" "${wall:-?}" "$(( ${rss:-0} / 1024 ))" "${vram:-n/a}" "$GDELTA" "$loss" "$rec" "$match" >> "$TSV"
    done
  done
done
cat "$TSV"
