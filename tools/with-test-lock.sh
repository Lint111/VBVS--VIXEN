#!/usr/bin/env bash
# Worktree-local entry point to the machine-wide build queue. The queue itself lives in the
# Undertow repository (tools/with-test-lock.sh); this file only locates it and execs it, so every
# argument, the working directory and the environment reach the real queue unchanged. It holds no
# queue logic: admission, locking, history and status all belong to the Undertow copy.
#
# Usage is identical to the Undertow wrapper:
#   bash tools/with-test-lock.sh --agent <lane> --resource <build|test|gpu|light> --label <lane>:<step> -- <cmd>
#   bash tools/with-test-lock.sh --help
#
# Where the Undertow queue is found, first match wins:
#   1. UT_QUEUE_SCRIPT   absolute path to the wrapper
#   2. UNDERTOW_ROOT     an Undertow checkout; uses $UNDERTOW_ROOT/tools/with-test-lock.sh
#   3. with-test-lock.sh on PATH (the machine-wide install)
#   4. ~/projects/undertow, ~/Github/undertow
# A named source (1 or 2) that is not usable is an error, never a silent fallback.
set -euo pipefail

self="$(readlink -f -- "${BASH_SOURCE[0]}")"

# A VIXEN lane and its Undertow consumer are siblings under the same
# .claude-worktrees directory. Export the paired consumer before locating the
# queue so queued codegen sees the same schema checkout as this lane without a
# hand-set UNDERTOW_ROOT. If the lane has no counterpart, use the sibling
# Undertow checkout; CMake performs the same pair-then-fallback derivation.
vixen_root="$(cd "$(dirname "$self")/.." && pwd -P)"
lane_root="$(dirname "$vixen_root")"
worktrees_root="$(dirname "$lane_root")"
provider_repo="$(dirname "$worktrees_root")"
projects_root="$(dirname "$provider_repo")"
lane_name="$(basename "$lane_root")"
for undertow_candidate in \
    "$projects_root/undertow/.claude-worktrees/$lane_name" \
    "$projects_root/undertow"; do
  if [[ -f "$undertow_candidate/core/src/Undertow.Authoring/Schema/schemas.json" ]]; then
    [[ -n "${UNDERTOW_ROOT:-}" ]] || export UNDERTOW_ROOT="$(cd "$undertow_candidate" && pwd -P)"
    break
  fi
done
export VIXEN_ROOT="${VIXEN_ROOT:-$vixen_root}"

usable() {
  local candidate real
  candidate="$1"
  [[ -f "$candidate" && -x "$candidate" ]] || return 1
  real="$(readlink -f -- "$candidate")"
  [[ "$real" != "$self" ]]
}

queue=""
if [[ -n "${WITH_TEST_LOCK_BIN:-}" ]]; then
  usable "$WITH_TEST_LOCK_BIN" || { echo "[with-test-lock] WITH_TEST_LOCK_BIN is not a usable queue script: $WITH_TEST_LOCK_BIN" >&2; exit 2; }
  queue="$WITH_TEST_LOCK_BIN"
elif [[ -n "${UT_QUEUE_SCRIPT:-}" ]]; then
  usable "$UT_QUEUE_SCRIPT" || { echo "[with-test-lock] UT_QUEUE_SCRIPT is not a usable queue script: $UT_QUEUE_SCRIPT" >&2; exit 2; }
  queue="$UT_QUEUE_SCRIPT"
elif [[ -n "${UNDERTOW_ROOT:-}" ]]; then
  usable "$UNDERTOW_ROOT/tools/with-test-lock.sh" || { echo "[with-test-lock] no queue script at UNDERTOW_ROOT: $UNDERTOW_ROOT/tools/with-test-lock.sh" >&2; exit 2; }
  queue="$UNDERTOW_ROOT/tools/with-test-lock.sh"
else
  for candidate in "$(command -v with-test-lock.sh 2>/dev/null || true)" \
                   "$HOME/projects/undertow/tools/with-test-lock.sh" \
                   "$HOME/Github/undertow/tools/with-test-lock.sh"; do
    if [[ -n "$candidate" ]] && usable "$candidate"; then
      queue="$candidate"
      break
    fi
  done
fi

if [[ -z "$queue" ]]; then
  echo "[with-test-lock] cannot find the Undertow build queue. Set UNDERTOW_ROOT to an Undertow checkout or UT_QUEUE_SCRIPT to its tools/with-test-lock.sh." >&2
  exit 2
fi

exec bash "$queue" "$@"
