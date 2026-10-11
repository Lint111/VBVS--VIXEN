#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 6 ]]; then
  echo "usage: $0 <scene> <width> <height> <config-ids> <rep> <tag>" >&2
  exit 2
fi

scene=$1
width=$2
height=$3
config_ids=$4
rep=$5
tag=$6
native_root=/mnt/c/vixen-native/vxtrain4
repo_root=$(git rev-parse --show-toplevel)
tip=$(git rev-parse HEAD)

(
  while true; do
    sleep 60
    echo "[heartbeat] vxtrain4 native look-dev matrix active $(date +%T)"
  done
) &
heartbeat_pid=$!
trap 'kill "$heartbeat_pid" 2>/dev/null || true' EXIT

git -C "$native_root" fetch "$repo_root" "$tip"
git -C "$native_root" checkout --detach FETCH_HEAD
cd /mnt/c
/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe \
  -NoProfile -ExecutionPolicy Bypass \
  -File 'C:\vixen-native\vxtrain4\reports\vxtrain4\reproduce\capture-matrix.ps1' \
  -ConfigIds "$config_ids" -Rep "$rep" -Tag "$tag" -Scene "$scene" \
  -Width "$width" -Height "$height"
