#!/usr/bin/env bash
# Run a CTest selection as two queue jobs split on the derived test class (R517).
#
#   cpu part  --resource test  ctest -j <cpu-jobs> -L '^cpu$'
#   gpu part  --resource gpu   one queued ctest shard per GPU worker
#
# The class labels come from the build (VixenTestClass.cmake + tests/gpu-class-manifest.tsv), not from
# this script. Both parts are submitted at once; the queue admits each against its own resource, so the
# CPU part never holds a GPU slot and the GPU part never holds CPU slots it does not use.
#
# Usage:
#   tools/run-tests-split.sh --build-dir build/wsl --agent mylane
#   tools/run-tests-split.sh --build-dir build/wsl --label 'RenderGraph|SVO|Application' --agent mylane
#   tools/run-tests-split.sh --build-dir build/wsl -- --timeout 120      # extra args go to both ctest runs
#
# Options:
#   --build-dir DIR   CMake build tree (default build/wsl)
#   --label RE        ctest -L suite selection, ANDed with the class label (narrows the default)
#   --all-correctness-labels  include every suite label containing cpu/gpu tests (the default)
#   --regex RE        ctest -R selection (default: everything)
#   --agent ID        queue fairness identity (default: worktree name)
#   --cpu-jobs N      ctest -j for the cpu part (default: nproc/2, max 8)
#   --gpu-jobs N      concurrent queued GPU shards (default 6; capped at the shared GPU limit)
#   --out DIR         logs and junit land here (default <build-dir>/split-run)
#   --witness         record GPU actions during the run (VIXEN_GPU_WITNESS_DIR=<out>/witness) so
#                     tools/classify-tests-gpu.py can narrow or relabel from the same run
#   --no-queue        run ctest directly (for hosts without the queue)
#   --exclude-labels RE  ctest -LE applied to both parts (default '^(native|nightly)$': the native-only
#                     planet aggregate and the nightly cache-collision gate stay out of lane runs, R565)
#   --nightly         include the nightly and native labels (clears --exclude-labels)
# Lane example:   bash tools/run-tests-split.sh --agent "$LANE"  (all correctness labels)
# Batch example:  one call per batch with no --regex; the exit status is non-zero if either part failed.
set -uo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
queue_wrapper="${VIXEN_BOX_QUEUE_WRAPPER:-/home/liory/.local/bin/with-test-lock.sh}"
build_dir="build/wsl"
regex=""
suite_label=""
all_correctness_labels=1
agent="$(basename "$root")"
cpu_jobs="$(( $(nproc) / 2 ))"
(( cpu_jobs < 1 )) && cpu_jobs=1
(( cpu_jobs > 8 )) && cpu_jobs=8
gpu_jobs=6
out_dir=""
witness=0
use_queue=1
exclude_labels='^(native|nightly)$'
extra=()

while (( $# )); do
  case "$1" in
    --build-dir) build_dir="$2"; shift 2 ;;
    --label) suite_label="$2"; all_correctness_labels=0; shift 2 ;;
    --all-correctness-labels) suite_label=""; all_correctness_labels=1; shift ;;
    --regex) regex="$2"; shift 2 ;;
    --agent) agent="$2"; shift 2 ;;
    --cpu-jobs) cpu_jobs="$2"; shift 2 ;;
    --gpu-jobs) gpu_jobs="$2"; shift 2 ;;
    --out) out_dir="$2"; shift 2 ;;
    --witness) witness=1; shift ;;
    --no-queue) use_queue=0; shift ;;
    --exclude-labels) exclude_labels="$2"; shift 2 ;;
    --nightly) exclude_labels=""; shift ;;
    --) shift; extra=("$@"); break ;;
    -h|--help) sed -n '2,/^set -uo/p' "${BASH_SOURCE[0]}" | sed '$d'; exit 0 ;;
    *) echo "[run-tests-split] unknown argument: $1" >&2; exit 2 ;;
  esac
done

[[ "$build_dir" = /* ]] || build_dir="$root/$build_dir"
[[ -f "$build_dir/CTestTestfile.cmake" ]] || { echo "[run-tests-split] not a configured build tree: $build_dir" >&2; exit 2; }

label_listing="$(ctest --test-dir "$build_dir" --print-labels)" || {
  echo "[run-tests-split] could not list CTest labels for $build_dir" >&2
  exit 2
}
mapfile -t available_labels < <(sed -n 's/^  //p' <<<"$label_listing")
exclude_args=()
[[ -n "$exclude_labels" ]] && exclude_args=(-LE "$exclude_labels")
suite_labels=()
for label in "${available_labels[@]}"; do
  case "$label" in
    ""|cpu|gpu|classified-*) continue ;;
  esac
  [[ -n "$exclude_labels" && "$label" =~ $exclude_labels ]] && continue
  suite_labels+=("$label")
done

ctest_count() {
  local output count
  output="$(ctest --test-dir "$build_dir" -N "${exclude_args[@]}" "$@")" || return $?
  count="$(sed -n 's/^Total Tests: //p' <<<"$output")"
  [[ "$count" =~ ^[0-9]+$ ]] || return 2
  printf '%s\n' "$count"
}

if (( all_correctness_labels )); then
  correctness_labels=()
  for label in "${suite_labels[@]}"; do
    count="$(ctest_count -L '^(cpu|gpu)$' -L "^${label}$")" || {
      echo "[run-tests-split] failed to count correctness tests for label '$label'" >&2
      exit 2
    }
    (( count > 0 )) && correctness_labels+=("$label")
  done
  if ((${#correctness_labels[@]} == 0)); then
    echo "[run-tests-split] no suite labels contain cpu/gpu correctness tests" >&2
    exit 2
  fi
  joined_labels="$(IFS='|'; printf '%s' "${correctness_labels[*]}")"
  suite_label="^(${joined_labels})$"

  all_cpu="$(ctest_count -L '^cpu$')" || exit 2
  all_gpu="$(ctest_count -L '^gpu$')" || exit 2
  selected_cpu="$(ctest_count -L '^cpu$' -L "$suite_label")" || exit 2
  selected_gpu="$(ctest_count -L '^gpu$' -L "$suite_label")" || exit 2
  if (( selected_cpu != all_cpu || selected_gpu != all_gpu )); then
    unlabelled_cpu=$((all_cpu - selected_cpu))
    unlabelled_gpu=$((all_gpu - selected_gpu))
    echo "[run-tests-split] suite labels omit classified tests (cpu $selected_cpu/$all_cpu, gpu $selected_gpu/$all_gpu); using all correctness tests (without suite label: cpu=$unlabelled_cpu gpu=$unlabelled_gpu)"
    suite_label=""
  else
    echo "[run-tests-split] selection=all-correctness-labels labels='$joined_labels'"
  fi
fi

[[ -n "$out_dir" ]] || out_dir="$build_dir/split-run"
mkdir -p "$out_dir"
rm -f "$out_dir"/cpu.log "$out_dir"/gpu.log "$out_dir"/cpu.xml "$out_dir"/gpu.xml
if (( witness )); then
  rm -rf "$out_dir/witness"; mkdir -p "$out_dir/witness"
fi

select_args=("${exclude_args[@]}")
[[ -n "$suite_label" ]] && select_args+=(-L "$suite_label")
[[ -n "$regex" ]] && select_args+=(-R "$regex")

cpu_selected="$(ctest_count -L '^cpu$' "${select_args[@]}")" || exit 2
gpu_selected="$(ctest_count -L '^gpu$' "${select_args[@]}")" || exit 2
if (( cpu_selected + gpu_selected == 0 )); then
  echo "[run-tests-split] selection matches no tests (label='${suite_label:-<all>}' regex='${regex:-<all>}'): refusing to report a pass" >&2
  exit 2
fi

icd="$HOME/.cache/vixen/wsl-vulkan/dzn_icd.json"
gpu_env=()
[[ -z "${VK_ICD_FILENAMES:-}" && -f "$icd" ]] && gpu_env=(VK_ICD_FILENAMES="$icd")
witness_env=()
(( witness )) && witness_env=(VIXEN_GPU_WITNESS_DIR="$out_dir/witness")

# CPU pair-tokens (two CPUs each) the largest PROCESSORS declaration among the part's selected tests needs;
# 0 when none declares one. The queue reserves them via --cpu-tokens, so a suite that needs concurrent
# CPUs is not admitted with fewer.
declared_cpu_tokens() {
  local part="$1"
  ctest --test-dir "$build_dir" --show-only=json-v1 -L "^${part}\$" "${select_args[@]}" | python3 -c '
import json, sys
tests = json.load(sys.stdin).get("tests", [])
need = 0
for test in tests:
    for prop in test.get("properties", []):
        if prop.get("name") == "PROCESSORS":
            need = max(need, int(prop.get("value", 0)))
print((need + 1) // 2)
'
}

queue() {
  local resource="$1" label="$2" cpu_tokens="$3"; shift 3
  if (( use_queue )); then
    local token_args=()
    (( cpu_tokens > 0 )) && token_args=(--cpu-tokens "$cpu_tokens")
    bash "$queue_wrapper" --agent "$agent" --resource "$resource" --label "$agent:$label" \
      ${token_args[@]+"${token_args[@]}"} -- "$@"
  else
    "$@"
  fi
}

run_gpu_shards() {
  local cpu_tokens="$1"; shift
  local shard_env=("$@")
  local shard_dir="$out_dir/gpu-shards"
  local started=$SECONDS
  mkdir -p "$shard_dir"

  # RESOURCE_LOCK is scoped to one CTest process. Generate exact-name regexes from the
  # filtered listing, then let the box queue admit each independent process as its own
  # shared GPU holder. The helper keeps fixture/dependency cases in shard zero.
  if ! ctest --test-dir "$build_dir" --show-only=json-v1 -L '^gpu$' \
      "${select_args[@]}" >"$shard_dir/tests.json"; then
    echo "[run-tests-split] could not enumerate GPU tests for sharding" >&2
    echo "2 $((SECONDS - started))" >"$out_dir/gpu.status"
    return 2
  fi
  local counts
  counts="$(python3 "$root/tools/shard-ctest-tests.py" \
      --json "$shard_dir/tests.json" --out-dir "$shard_dir" --shards "$gpu_jobs")" || {
    echo "[run-tests-split] could not shard GPU tests" >&2
    echo "2 $((SECONDS - started))" >"$out_dir/gpu.status"
    return 2
  }
  echo "[run-tests-split] GPU shard counts: $counts"

  local shard_total=0
  local -a pids=()
  local index count shard_regex shard_started shard_status
  for ((index = 0; index < gpu_jobs; ++index)); do
    count="$(<"$shard_dir/shard-$index.count")"
    shard_total=$((shard_total + count))
    (( count > 0 )) || continue
    shard_regex="$(<"$shard_dir/shard-$index.regex")"
    (
      shard_started=$SECONDS
      if (( use_queue )); then
        local token_args=()
        (( cpu_tokens > 0 )) && token_args=(--cpu-tokens "$cpu_tokens")
        bash "$queue_wrapper" --agent "${agent}-gpu${index}" --resource gpu \
          --label "$agent:split-gpu-$index" "${token_args[@]}" -- \
          env "${witness_env[@]}" "${shard_env[@]}" \
          ctest --test-dir "$build_dir" -j 1 -L '^gpu$' "${exclude_args[@]}" \
            -R "$shard_regex" --no-tests=error --output-on-failure \
            --output-junit "$shard_dir/shard-$index.xml" "${extra[@]}" \
          >"$shard_dir/shard-$index.log" 2>&1
      else
        env "${witness_env[@]}" "${shard_env[@]}" \
          ctest --test-dir "$build_dir" -j 1 -L '^gpu$' "${exclude_args[@]}" \
            -R "$shard_regex" --no-tests=error --output-on-failure \
            --output-junit "$shard_dir/shard-$index.xml" "${extra[@]}" \
          >"$shard_dir/shard-$index.log" 2>&1
      fi
      shard_status=$?
      echo "$shard_status $((SECONDS - shard_started))" >"$shard_dir/shard-$index.status"
      exit "$shard_status"
    ) &
    pids+=("$!")
  done

  if (( shard_total != gpu_selected )); then
    echo "[run-tests-split] GPU sharding selected $shard_total tests, expected $gpu_selected" >&2
    for pid in "${pids[@]}"; do kill "$pid" 2>/dev/null || true; done
    echo "2 $((SECONDS - started))" >"$out_dir/gpu.status"
    return 2
  fi

  local gpu_rc=0
  for pid in "${pids[@]}"; do
    wait "$pid" || gpu_rc=1
  done
  : >"$out_dir/gpu.log"
  for ((index = 0; index < gpu_jobs; ++index)); do
    [[ -f "$shard_dir/shard-$index.log" ]] || continue
    echo "[run-tests-split] --- GPU shard $index ---" >>"$out_dir/gpu.log"
    sed -n '1,$p' "$shard_dir/shard-$index.log" >>"$out_dir/gpu.log"
  done
  echo "$gpu_rc $((SECONDS - started))" >"$out_dir/gpu.status"
  return "$gpu_rc"
}

run_part() {
  local part="$1" resource="$2" jobs="$3" selected="$4"; shift 4
  local started=$SECONDS cpu_tokens
  if (( selected == 0 )); then
    : >"$out_dir/$part.log"
    echo "0 0" >"$out_dir/$part.status"
    return 0
  fi
  cpu_tokens="$(declared_cpu_tokens "$part")" || cpu_tokens=0
  (( cpu_tokens > 0 )) && echo "[run-tests-split] $part: PROCESSORS declaration needs cpu-tokens=$cpu_tokens"
  if [[ "$part" == gpu && "$gpu_jobs" -gt 1 ]]; then
    run_gpu_shards "$cpu_tokens" "$@"
    return $?
  fi
  queue "$resource" "split-$part" "$cpu_tokens" \
    env "${witness_env[@]}" "$@" \
    ctest --test-dir "$build_dir" -j "$jobs" -L "^${part}\$" "${select_args[@]}" \
          --no-tests=error --output-on-failure --output-junit "$out_dir/$part.xml" "${extra[@]}" \
    >"$out_dir/$part.log" 2>&1
  local status=$?
  echo "$status $((SECONDS - started))" >"$out_dir/$part.status"
  return "$status"
}

echo "[run-tests-split] build=$build_dir label='${suite_label:-<all correctness labels>}' regex='${regex:-<all>}' exclude-labels='${exclude_labels:-<none>}' cpu-jobs=$cpu_jobs gpu-jobs=$gpu_jobs agent=$agent"
echo "[run-tests-split] cpu: $cpu_selected   gpu: $gpu_selected"
for label in "${suite_labels[@]}"; do
  label_cpu="$(ctest_count -L '^cpu$' "${select_args[@]}" -L "^${label}$")" || exit 2
  label_gpu="$(ctest_count -L '^gpu$' "${select_args[@]}" -L "^${label}$")" || exit 2
  echo "[run-tests-split] label=$label cpu=$label_cpu gpu=$label_gpu selected=$((label_cpu + label_gpu))"
done

wall_start=$SECONDS
run_part cpu test "$cpu_jobs" "$cpu_selected" &
cpu_pid=$!
run_part gpu gpu "$gpu_jobs" "$gpu_selected" ${gpu_env[@]+"${gpu_env[@]}"} &
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
