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

(
  while true; do
    sleep 60
    echo "[heartbeat] travdefault capture active $(date +%T)"
  done
) &
heartbeat_pid=$!
trap 'kill "$heartbeat_pid" 2>/dev/null || true' EXIT

cd /mnt/c
/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe \
  -NoProfile -ExecutionPolicy Bypass \
  -File 'C:\vixen-native\travdefault\reports\travdefault\reproduce\capture-matrix.ps1' \
  -ConfigIds "$config_ids" -Rep "$rep" -Tag "$tag" -Scene "$scene" \
  -Width "$width" -Height "$height"
