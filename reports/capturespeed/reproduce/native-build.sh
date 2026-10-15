#!/usr/bin/env bash
# Sync this exact VIXEN commit to its lane-isolated NTFS clone and build its capture tests.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../../.." && pwd)"
repo_commit="$(git -C "$repo_root" rev-parse HEAD)"
undertow_root="${UNDERTOW_ROOT:-}"
# Each lane keeps its own source/build checkout. The native runner and GPU queue are
# still shared; only the mutable Git working tree is isolated so concurrent lanes
# cannot change the source under another lane's CMake configure or CTest process.
native_root=/mnt/c/vixen-native/nativerunner-textshader
queue=/home/liory/.local/bin/with-test-lock.sh
cmd_exe=/mnt/c/Windows/System32/cmd.exe
native_batch="$native_root/reports/capturespeed/reproduce/native-build.bat"

if [[ -n "$(git -C "$repo_root" status --porcelain)" ]]; then
  echo "native-build: refusing to sync a dirty VIXEN tree; commit the runner change first" >&2
  exit 2
fi
if [[ -z "$undertow_root" ]]; then
  echo "native-build: set UNDERTOW_ROOT to the paired Undertow worktree" >&2
  exit 2
fi
if [[ ! -d "$undertow_root/core/src/Undertow.Authoring/Schema" ]]; then
  echo "native-build: UNDERTOW_ROOT does not contain core/src/Undertow.Authoring/Schema: $undertow_root" >&2
  exit 2
fi
undertow_root="$(cd "$undertow_root" && pwd -P)"
undertow_root_win="$(wslpath -m "$undertow_root")"

native_batch_win="$(wslpath -w "$native_batch")"
bash "$queue" --agent nativerunner --resource build --label "nativerunner:native-build-${repo_commit:0:12}" -- bash -c '
    set -euo pipefail
    native_root="$1"
    repo_root="$2"
    repo_commit="$3"
    native_batch_win="$4"
    cmd_exe="$5"
    undertow_root_win="$6"
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
        printf "[heartbeat] nativerunner native Windows build active %s\\n" "$(date +%T)"
      done
    }
    heartbeat &
    heartbeat_pid=$!
    cleanup() {
      kill "$heartbeat_pid" 2>/dev/null || true
      wait "$heartbeat_pid" 2>/dev/null || true
    }
    trap cleanup EXIT INT TERM
    "$cmd_exe" /d /c "$native_batch_win $repo_commit $undertow_root_win"
  ' nativerunner-native-build "$native_root" "$repo_root" "$repo_commit" "$native_batch_win" "$cmd_exe" "$undertow_root_win"
