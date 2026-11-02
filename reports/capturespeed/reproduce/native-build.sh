#!/usr/bin/env bash
# Sync this exact VIXEN commit to its lane-isolated NTFS clone and build its capture tests.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../../.." && pwd)"
undertow_root="${UNDERTOW_ROOT:-}"
source "$script_dir/native-common.sh"
queue=/home/liory/.local/bin/with-test-lock.sh
cmd_exe="$native_cmd_exe"
native_batch="$native_root/reports/capturespeed/reproduce/native-build.bat"

native_require_interop
if [[ -z "$undertow_root" ]]; then
  echo "native-build: set UNDERTOW_ROOT to the paired Undertow worktree" >&2
  exit 2
fi
if [[ ! -d "$undertow_root/core/src/Undertow.Authoring/Schema" ]]; then
  echo "native-build: UNDERTOW_ROOT does not contain core/src/Undertow.Authoring/Schema: $undertow_root" >&2
  exit 2
fi
undertow_root="$(cd "$undertow_root" && pwd -P)"
# Forward slashes: backslash UNC paths break CMake string escapes (\U) in VerifyGlobs.cmake.
undertow_root_win="$(wslpath -m "$undertow_root")"
undertow_id="$(native_undertow_identity "$undertow_root")"
repo_commit="$(native_resolve_commit)"

native_batch_win="$(wslpath -w "$native_batch")"
bash "$queue" --agent nativerunner --resource build --label "nativerunner:native-build-${native_lane_id}-${repo_commit:0:12}" -- bash -c '
    set -euo pipefail
    native_root="$1"
    repo_root="$2"
    repo_commit="$3"
    native_batch_win="$4"
    cmd_exe="$5"
    undertow_root_win="$6"
    undertow_id="$7"
    native_lane_id="$8"
    if [[ ! -d "$native_root/.git" ]]; then
      mkdir -p "$(dirname "$native_root")"
      git clone --no-hardlinks "$repo_root" "$native_root"
    fi
    # Previous builds write tracked SPIR-V headers and copy this ignored preset into the lane clone.
    git -C "$native_root" reset --hard
    rm -f "$native_root/VIXEN/CMakeUserPresets.json"
    # Keep exact-commit sync and build together in the shared build admission.
    git -C "$native_root" fetch "$repo_root" "$repo_commit"
    git -C "$native_root" checkout --force --detach FETCH_HEAD
    [[ "$(git -C "$native_root" rev-parse HEAD)" == "$repo_commit" ]]
    heartbeat() {
      while :; do
        sleep 60
        printf "[heartbeat] native Windows build %s active %s\\n" "$native_lane_id" "$(date +%T)"
      done
    }
    heartbeat &
    heartbeat_pid=$!
    cleanup() {
      kill "$heartbeat_pid" 2>/dev/null || true
      wait "$heartbeat_pid" 2>/dev/null || true
    }
    trap cleanup EXIT INT TERM
    "$cmd_exe" /d /c "$native_batch_win $repo_commit $undertow_root_win $undertow_id"
  ' nativerunner-native-build "$native_root" "$repo_root" "$repo_commit" "$native_batch_win" "$cmd_exe" "$undertow_root_win" "$undertow_id" "$native_lane_id"
