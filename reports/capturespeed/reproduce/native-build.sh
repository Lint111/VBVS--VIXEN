#!/usr/bin/env bash
# Sync this exact VIXEN commit to its persistent NTFS clone and build its capture tests.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../../.." && pwd)"
repo_commit="$(git -C "$repo_root" rev-parse HEAD)"
native_root=/mnt/c/vixen-native/nativerunner
queue=/home/liory/.local/bin/with-test-lock.sh
cmd_exe=/mnt/c/Windows/System32/cmd.exe
native_batch="$native_root/reports/capturespeed/reproduce/native-build.bat"

if [[ -n "$(git -C "$repo_root" status --porcelain)" ]]; then
  echo "native-build: refusing to sync a dirty VIXEN tree; commit the runner change first" >&2
  exit 2
fi

if [[ ! -d "$native_root/.git" ]]; then
  mkdir -p "${native_root%/*}"
  git clone --no-hardlinks "$repo_root" "$native_root"
fi

# The Windows clone is stable per lane; CMake output is keyed by commit below build/native/.
git -C "$native_root" fetch "$repo_root" "$repo_commit"
git -C "$native_root" checkout --detach FETCH_HEAD
[[ "$(git -C "$native_root" rev-parse HEAD)" == "$repo_commit" ]]

native_batch_win="$(wslpath -w "$native_batch")"
native_command="$native_batch_win $repo_commit"
if [[ -n "${UNDERTOW_ROOT:-}" ]]; then
  if [[ ! -d "$UNDERTOW_ROOT/core/src/Undertow.Authoring/Schema" ]]; then
    echo "native-build: UNDERTOW_ROOT does not contain core/src/Undertow.Authoring/Schema: $UNDERTOW_ROOT" >&2
    exit 2
  fi
  undertow_root_win="$(wslpath -w "$UNDERTOW_ROOT")"
  native_command="$native_batch_win $repo_commit $undertow_root_win"
fi
bash "$queue" --agent nativerunner --resource build \
  --label "nativerunner:native-build-${repo_commit:0:12}" -- \
  bash -c '
    set -euo pipefail
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
    "$@"
  ' nativerunner-native-build "$cmd_exe" /d /c "$native_command"
