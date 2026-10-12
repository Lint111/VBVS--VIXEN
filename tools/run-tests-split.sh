#!/usr/bin/env bash
# Run a CTest selection as two queue jobs split on the derived test class (R517).
#
#   cpu part  --resource test  ctest -j <cpu-jobs> -L '^cpu$'
#   gpu part  --resource gpu   ctest -j <gpu-jobs> -L '^gpu$'    (serial by default)
#
# The class labels come from the build (VixenTestClass.cmake + tests/gpu-class-manifest.tsv), not from
# this script. Both parts are submitted at once; the queue admits each against its own resource, so the
# CPU part never holds a GPU slot and the GPU part never holds CPU slots it does not use.
#
# Usage:
#   tools/run-tests-split.sh --build-dir build/wsl --label 'RenderGraph|SVO' --agent mylane
#   tools/run-tests-split.sh --build-dir build/wsl -- --timeout 120      # extra args go to both ctest runs
#
# Options:
#   --build-dir DIR   CMake build tree (default build/wsl)
#   --label RE        ctest -L suite selection, ANDed with the class label (e.g. 'RenderGraph|SVO')
#   --regex RE        ctest -R selection (default: everything)
#   --agent ID        queue fairness identity (default: worktree name)
#   --cpu-jobs N      ctest -j for the cpu part (default: nproc/2, max 8)
#   --gpu-jobs N      ctest -j for the gpu part (default 1; keep <= the shared GPU cap)
#   --out DIR         logs and junit land here (default <build-dir>/split-run)
#   --witness         record GPU actions during the run (VIXEN_GPU_WITNESS_DIR=<out>/witness) so
#                     tools/classify-tests-gpu.py can narrow or relabel from the same run
#   --no-queue        run ctest directly (for hosts without the queue)
# Lane example:   bash tools/run-tests-split.sh --agent "$LANE" --label 'RenderGraph|SVO'
# Batch example:  one call per batch with no --regex; the exit status is non-zero if either part failed.
set -uo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
queue_wrapper="${VIXEN_BOX_QUEUE_WRAPPER:-/home/liory/.local/bin/with-test-lock.sh}"
build_dir="build/wsl"
regex=""
suite_label=""
agent="$(basename "$root")"
cpu_jobs="$(( $(nproc) / 2 ))"
(( cpu_jobs < 1 )) && cpu_jobs=1
(( cpu_jobs > 8 )) && cpu_jobs=8
gpu_jobs=1
out_dir=""
witness=0
use_queue=1
extra=()

while (( $# )); do
  case "$1" in
    --build-dir) build_dir="$2"; shift 2 ;;
    --label) suite_label="$2"; shift 2 ;;
    --regex) regex="$2"; shift 2 ;;
    --agent) agent="$2"; shift 2 ;;
    --cpu-jobs) cpu_jobs="$2"; shift 2 ;;
    --gpu-jobs) gpu_jobs="$2"; shift 2 ;;
    --out) out_dir="$2"; shift 2 ;;
    --witness) witness=1; shift ;;
    --no-queue) use_queue=0; shift ;;
    --) shift; extra=("$@"); break ;;
    -h|--help) sed -n '2,/^set -uo/p' "${BASH_SOURCE[0]}" | sed '$d'; exit 0 ;;
    *) echo "[run-tests-split] unknown argument: $1" >&2; exit 2 ;;
  esac
done

[[ "$build_dir" = /* ]] || build_dir="$root/$build_dir"
[[ -f "$build_dir/CTestTestfile.cmake" ]] || { echo "[run-tests-split] not a configured build tree: $build_dir" >&2; exit 2; }
[[ -n "$out_dir" ]] || out_dir="$build_dir/split-run"
mkdir -p "$out_dir"
rm -f "$out_dir"/cpu.log "$out_dir"/gpu.log "$out_dir"/cpu.xml "$out_dir"/gpu.xml
if (( witness )); then
  rm -rf "$out_dir/witness"; mkdir -p "$out_dir/witness"
fi

select_args=()
[[ -n "$suite_label" ]] && select_args+=(-L "$suite_label")
[[ -n "$regex" ]] && select_args+=(-R "$regex")

icd="$HOME/.cache/vixen/wsl-vulkan/dzn_icd.json"
gpu_env=()
[[ -z "${VK_ICD_FILENAMES:-}" && -f "$icd" ]] && gpu_env=(VK_ICD_FILENAMES="$icd")
witness_env=()
(( witness )) && witness_env=(VIXEN_GPU_WITNESS_DIR="$out_dir/witness")

queue() {
  local resource="$1" label="$2"; shift 2
  if (( use_queue )); then
    bash "$queue_wrapper" --agent "$agent" --resource "$resource" --label "$agent:$label" -- "$@"
  else
    "$@"
  fi
}

run_part() {
  local part="$1" resource="$2" jobs="$3"; shift 3
  local started=$SECONDS
  queue "$resource" "split-$part" \
    env "${witness_env[@]}" "$@" \
    ctest --test-dir "$build_dir" -j "$jobs" -L "^${part}\$" "${select_args[@]}" \
          --output-on-failure --output-junit "$out_dir/$part.xml" "${extra[@]}" \
    >"$out_dir/$part.log" 2>&1
  local status=$?
  echo "$status $((SECONDS - started))" >"$out_dir/$part.status"
  return "$status"
}

echo "[run-tests-split] build=$build_dir label='${suite_label:-<all>}' regex='${regex:-<all>}' cpu-jobs=$cpu_jobs gpu-jobs=$gpu_jobs agent=$agent"
echo "[run-tests-split] cpu: $(ctest --test-dir "$build_dir" -N -L '^cpu$' "${select_args[@]}" | sed -n 's/^Total Tests: //p')   gpu: $(ctest --test-dir "$build_dir" -N -L '^gpu$' "${select_args[@]}" | sed -n 's/^Total Tests: //p')"

wall_start=$SECONDS
run_part cpu test "$cpu_jobs" &
cpu_pid=$!
run_part gpu gpu "$gpu_jobs" ${gpu_env[@]+"${gpu_env[@]}"} &
gpu_pid=$!
wait "$cpu_pid"; cpu_rc=$?
wait "$gpu_pid"; gpu_rc=$?
wall=$((SECONDS - wall_start))

for part in cpu gpu; do
  read -r rc secs <"$out_dir/$part.status" 2>/dev/null || { rc="?"; secs="?"; }
  summary="$(grep -E 'tests passed' "$out_dir/$part.log" | tail -1)"
  echo "[run-tests-split] $part: exit=$rc wall=${secs}s ${summary:-no ctest summary (see $out_dir/$part.log)}"
  grep -E '^\s+[0-9]+ - .*\((Failed|Timeout|Not Run|SEGFAULT|Subprocess aborted|Exception)' "$out_dir/$part.log" | sed 's/^/    /'
done
echo "[run-tests-split] total wall=${wall}s (parts overlap; queue admission may serialize them)"

(( cpu_rc == 0 && gpu_rc == 0 ))
