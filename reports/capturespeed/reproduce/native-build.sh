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
bash "$queue" --agent nativerunner --resource build \
  --label "nativerunner:native-build-${repo_commit:0:12}" -- \
  "$cmd_exe" /d /c "$native_batch_win $repo_commit"
