#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 || ! "$1" =~ ^[0-9a-f]{40}$ ]]; then
  echo "usage: $0 <40-character-commit-sha>" >&2
  exit 2
fi
revision=$1
native_root=/mnt/c/vixen-native/vxtrain6
repo_root=$(git rev-parse --show-toplevel)
harness="$native_root/.tmp/vxtrain6-harness"
(
  while true; do
    sleep 60
    echo "[heartbeat] vxtrain6 native matrix build ${revision:0:8} active $(date +%T)"
  done
) &
heartbeat_pid=$!
trap 'kill "$heartbeat_pid" 2>/dev/null || true' EXIT

if [[ ! -d "$native_root/.git" ]]; then
  echo "Assigned native checkout is missing: $native_root" >&2
  exit 1
fi
mkdir -p "$harness"
cp "$repo_root/reports/vxtrain6/reproduce/matrix-build.bat" "$harness/matrix-build.bat"
cp "$repo_root/reports/vxtrain6/reproduce/configs.json" "$harness/configs.json"
cp "$repo_root/reports/vxtrain6/reproduce/CMakeUserPresets.json" "$harness/CMakeUserPresets.json"
git -C "$native_root" fetch "$repo_root" "$revision"
git -C "$native_root" checkout --detach FETCH_HEAD
cd /mnt/c
nice -n 10 /mnt/c/Windows/System32/cmd.exe /c \
  "C:\vixen-native\vxtrain6\.tmp\vxtrain6-harness\matrix-build.bat"
