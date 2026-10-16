#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 || ! "$1" =~ ^[0-9a-f]{40}$ || ! "$2" =~ ^[a-z0-9-]+$ ]]; then
  echo "usage: $0 <40-character-commit-sha> <tag>" >&2
  exit 2
fi
revision=$1
tag=$2
native_root=/mnt/c/vixen-native/vxtrain6
repo_root=$(git rev-parse --show-toplevel)
harness="$native_root/.tmp/vxtrain6-harness"
(
  while true; do
    sleep 60
    echo "[heartbeat] vxtrain6 native matrix ${tag} active $(date +%T)"
  done
) &
heartbeat_pid=$!
trap 'kill "$heartbeat_pid" 2>/dev/null || true' EXIT

if [[ ! -d "$native_root/.git" ]]; then
  echo "Assigned native checkout is missing: $native_root" >&2
  exit 1
fi
mkdir -p "$harness"
cp "$repo_root/reports/vxtrain6/reproduce/capture-matrix.ps1" "$harness/capture-matrix.ps1"
cp "$repo_root/reports/vxtrain6/reproduce/configs.json" "$harness/configs.json"
git -C "$native_root" fetch "$repo_root" "$revision"
git -C "$native_root" checkout --detach FETCH_HEAD
cd /mnt/c
/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe \
  -NoProfile -ExecutionPolicy Bypass \
  -File 'C:\vixen-native\vxtrain6\.tmp\vxtrain6-harness\capture-matrix.ps1' \
  -Tag "$tag" -ConfigIds 'default,esvo' -Rep 1 -Width 1920 -Height 1080
