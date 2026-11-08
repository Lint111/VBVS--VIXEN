#!/usr/bin/env bash
# Run a long CTest repeat gate as short serial queue jobs with a resume marker (T-2657).
#
# One `ctest --repeat until-fail:50` is a single queue payload and hit the queue's ~550s
# WRAPPER_SIGNALED/143 limit after five runs. This splits the same gate into batches of
# --batch runs; each batch is its own queue job, so no payload outlives the session limit. After
# every passing batch the completed run count is written to the marker, and a rerun with the same
# selection continues from there. A failed batch stops the gate and leaves the marker at the last
# passing run; a changed selection (regex/total/batch/build tree) discards a stale marker.
#
# Usage:
#   tools/ctest-repeat-batches.sh --build-dir build/wsl --regex '^cornell_capture$' --total 50 --batch 5 --agent mylane
#
# Options:
#   --build-dir DIR   CMake build tree (default build/wsl)
#   --regex RE        ctest -R selection (required)
#   --total N         total passing runs required (default 50)
#   --batch N         runs per queue job (default 5)
#   --resource R      queue resource: test|gpu|light|build (default test)
#   --agent ID        queue identity (default: worktree name)
#   --state FILE      resume marker (default <build-dir>/ctest-repeat/<regex-hash>.state)
#   --fresh           ignore and reset an existing marker
#   --no-queue        run ctest directly (for hosts without the queue)
#   -- ARGS           extra arguments for ctest
# Exit status is 0 only when all --total runs passed; the final line states the completed count.
set -uo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
queue_wrapper="${VIXEN_BOX_QUEUE_WRAPPER:-$root/tools/with-test-lock.sh}"
build_dir="build/wsl"; regex=""; total=50; batch=5; resource="test"
agent="$(basename "$root")"; state=""; fresh=0; use_queue=1; extra=()

while (( $# )); do
  case "$1" in
    --build-dir) build_dir="$2"; shift 2 ;;
    --regex) regex="$2"; shift 2 ;;
    --total) total="$2"; shift 2 ;;
    --batch) batch="$2"; shift 2 ;;
    --resource) resource="$2"; shift 2 ;;
    --agent) agent="$2"; shift 2 ;;
    --state) state="$2"; shift 2 ;;
    --fresh) fresh=1; shift ;;
    --no-queue) use_queue=0; shift ;;
    --) shift; extra=("$@"); break ;;
    -h|--help) sed -n '2,/^set -uo/p' "${BASH_SOURCE[0]}" | sed '$d'; exit 0 ;;
    *) echo "[ctest-repeat] unknown argument: $1" >&2; exit 2 ;;
  esac
done

[[ -n "$regex" ]] || { echo "[ctest-repeat] --regex is required" >&2; exit 2; }
[[ "$total" =~ ^[1-9][0-9]*$ && "$batch" =~ ^[1-9][0-9]*$ ]] || { echo "[ctest-repeat] --total and --batch must be positive integers" >&2; exit 2; }
[[ "$build_dir" = /* ]] || build_dir="$root/$build_dir"
[[ -f "$build_dir/CTestTestfile.cmake" ]] || { echo "[ctest-repeat] not a configured build tree: $build_dir" >&2; exit 2; }

selected="$(ctest --test-dir "$build_dir" -N -R "$regex" | sed -n 's/^Total Tests: //p')"
[[ "$selected" =~ ^[0-9]+$ && "$selected" -gt 0 ]] || { echo "[ctest-repeat] regex '$regex' matches no tests: refusing to report a pass" >&2; exit 2; }

key="regex=$regex total=$total batch=$batch build=$build_dir"
if [[ -z "$state" ]]; then
  hash="$(printf '%s' "$regex" | cksum | cut -d' ' -f1)"
  state="$build_dir/ctest-repeat/$hash.state"
fi
mkdir -p "$(dirname "$state")"

done_runs=0
if (( ! fresh )) && [[ -f "$state" ]]; then
  { IFS= read -r saved_key; IFS= read -r saved_done; } <"$state"
  if [[ "$saved_key" == "$key" && "$saved_done" =~ ^[0-9]+$ ]]; then
    done_runs="$saved_done"
    echo "[ctest-repeat] resuming at $done_runs/$total completed ($state)"
  else
    echo "[ctest-repeat] marker is for a different selection; starting from 0"
  fi
fi

record() { printf '%s\n%s\n' "$key" "$1" >"$state"; }
record "$done_runs"

while (( done_runs < total )); do
  n=$(( total - done_runs < batch ? total - done_runs : batch ))
  echo "[ctest-repeat] batch runs $((done_runs + 1))-$((done_runs + n)) of $total"
  cmd=(ctest --test-dir "$build_dir" -R "$regex" --repeat "until-fail:$n" --no-tests=error --output-on-failure ${extra[@]+"${extra[@]}"})
  if (( use_queue )); then
    bash "$queue_wrapper" --agent "$agent" --resource "$resource" \
      --label "$agent:repeat-$((done_runs + 1))-$((done_runs + n))" -- "${cmd[@]}"
  else
    "${cmd[@]}"
  fi
  rc=$?
  if (( rc != 0 )); then
    echo "[ctest-repeat] FAILED in batch starting at run $((done_runs + 1)) (exit $rc); $done_runs/$total completed; rerun to resume" >&2
    exit "$rc"
  fi
  done_runs=$(( done_runs + n ))
  record "$done_runs"
done
echo "[ctest-repeat] PASSED $done_runs/$total runs of '$regex'"
