#!/usr/bin/env bash
# VIXEN codegen gate: builds every generated-contract `*_check` target of a configured build tree as ONE
# queued light job. The target set is derived from the build graph (ninja -t targets), so a new
# vixen_add_codegen_check() is covered without editing this script. It replaces the shared
# undertow tools/check-content-codegen.sh, which does not exist in a VIXEN checkout (exit 127).
#
# Usage: tools/check-vixen-codegen.sh [--build-dir DIR] [--agent ID] [--label L] [--list] [--no-queue]
#   --build-dir DIR  configured CMake/Ninja tree (default build/wsl); never configured here
#   --agent ID       queue identity (default: worktree name)
#   --list           print the derived check targets and exit (no queue, no build)
#   --no-queue       run in this process (this is what the queued job runs; do not wrap it yourself)
# Queue-aware: do not wrap it in your own with-test-lock job.
set -uo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
queue_wrapper="${VIXEN_BOX_QUEUE_WRAPPER:-/home/liory/.local/bin/with-test-lock.sh}"
build_dir="build/wsl"
agent="$(basename "$root")"
label=""
list_only=0
queued=1

while (( $# )); do
  case "$1" in
    --build-dir) build_dir="${2:?--build-dir needs a value}"; shift 2 ;;
    --agent) agent="${2:?--agent needs a value}"; shift 2 ;;
    --label) label="${2:?--label needs a value}"; shift 2 ;;
    --list) list_only=1; shift ;;
    --no-queue) queued=0; shift ;;
    -h|--help) sed -n '2,12p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "[check-vixen-codegen] unknown argument: $1 (see --help)" >&2; exit 2 ;;
  esac
done
[[ "$build_dir" = /* ]] || build_dir="$root/$build_dir"

if [[ ! -f "$build_dir/build.ninja" ]]; then
  echo "[check-vixen-codegen] ERROR: $build_dir/build.ninja not found: no configured Ninja tree there." >&2
  echo "[check-vixen-codegen] FIX: configure first (cd VIXEN && cmake --preset vixen-wsl) or pass --build-dir <configured tree>." >&2
  exit 2
fi

mapfile -t targets < <(ninja -C "$build_dir" -t targets all 2>/dev/null | sed -n 's/^\([A-Za-z0-9_]*_check\): .*/\1/p' | sort -u)
if (( ${#targets[@]} == 0 )); then
  echo "[check-vixen-codegen] ERROR: no *_check targets in $build_dir/build.ninja: codegen checks are not part of this configuration (dotnet/Yeroket tool not found at configure time?)." >&2
  echo "[check-vixen-codegen] FIX: re-run cmake configure and read its [codegen] WARNING lines." >&2
  exit 2
fi
if (( list_only )); then printf '%s\n' "${targets[@]}"; exit 0; fi

if (( queued )); then
  exec bash "$queue_wrapper" --agent "$agent" --resource light --label "${label:-$agent:vixen-codegen}" -- \
    bash "$root/tools/check-vixen-codegen.sh" --no-queue --build-dir "$build_dir"
fi

# Same setting the configured codegen runner exports (VIXEN/codegen/CMakeLists.txt); the tool-restore step
# aborts on hosts without libicu otherwise.
export DOTNET_SYSTEM_GLOBALIZATION_INVARIANT="${DOTNET_SYSTEM_GLOBALIZATION_INVARIANT:-1}"
echo "[check-vixen-codegen] ${#targets[@]} check targets in $build_dir"
# Node width capped by the memory available now (R847.4); 2048 MB per check job is an unmeasured conservative start.
source "$root/tools/node-width.sh"
check_jobs="$(nw_jobs "$(nw_logical_cores)" "$(nw_wsl_available_mb)" 2048)"
echo "[check-vixen-codegen] ninja -j$check_jobs (cores=$(nw_logical_cores) available_mb=$(nw_wsl_available_mb) per_job_mb=2048)"
if ! nice -n 10 ninja -C "$build_dir" -j"$check_jobs" -k 0 "${targets[@]}"; then
  echo "[check-vixen-codegen] FAILED: a check target failed in $build_dir (either generated output drifted from canonical, or the codegen tool itself failed to build/run: read the first FAILED: block above)." >&2
  echo "[check-vixen-codegen] FIX: for drift run the matching <name>_regen target (ninja -C $build_dir <name>_regen) and commit the result; for a tool failure fix the toolchain named in the block." >&2
  exit 1
fi
echo "[check-vixen-codegen] OK: ${#targets[@]} codegen checks passed"
