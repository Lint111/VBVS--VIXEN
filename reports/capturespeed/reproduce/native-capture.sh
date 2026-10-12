#!/usr/bin/env bash
# usage: native-capture.sh <tag> [state] [ExtraEnv "K=V;K=V"]  -- run from the queue with --resource gpu --gpu-benchmark
set -euo pipefail
tag="$1"; state="${2:-midday}"; extra="${3:-}"
git -C /mnt/c/vixen-native/capturespeed fetch /home/liory/projects/VBVS--VIXEN/.claude-worktrees/capturespeed lane-capturespeed >/dev/null 2>&1
git -C /mnt/c/vixen-native/capturespeed merge --ff-only FETCH_HEAD >/dev/null 2>&1 || true
( while true; do sleep 30; echo "[heartbeat] native capture running $(date +%T)"; done ) &
hb=$!
trap 'kill "$hb" 2>/dev/null || true' EXIT
cd /mnt/c
/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe -NoProfile -ExecutionPolicy Bypass \
  -File 'C:\vixen-native\capturespeed\reports\capturespeed\reproduce\native-capture.ps1' -Tag "$tag" -State "$state" -ExtraEnv "$extra"
