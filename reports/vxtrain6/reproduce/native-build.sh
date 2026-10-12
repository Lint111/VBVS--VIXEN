#!/usr/bin/env bash
set -euo pipefail

native_root=/mnt/c/vixen-native/vxtrain6
repo_root=$(git rev-parse --show-toplevel)
tip=$(git rev-parse HEAD)
(
  while true; do
    sleep 60
    echo "[heartbeat] vxtrain6 native build active $(date +%T)"
  done
) &
heartbeat_pid=$!
trap 'kill "$heartbeat_pid" 2>/dev/null || true' EXIT

if [[ ! -d "$native_root/.git" ]]; then
  echo "Assigned native checkout is missing: $native_root" >&2
  exit 1
fi

git -C "$native_root" fetch "$repo_root" "$tip"
git -C "$native_root" checkout --detach FETCH_HEAD
cd /mnt/c
nice -n 10 /mnt/c/Windows/System32/cmd.exe /c \
  "C:\vixen-native\vxtrain6\reports\vxtrain6\reproduce\native-build.bat"
