#!/usr/bin/env bash
# Sync the lane branch into the native clone and build vixen_lookdev_capture with MSVC (-j4).
set -euo pipefail
WT=/home/liory/projects/VBVS--VIXEN/.claude-worktrees/capturespeed
git -C /mnt/c/vixen-native/capturespeed fetch "$WT" lane-capturespeed
git -C /mnt/c/vixen-native/capturespeed merge --ff-only FETCH_HEAD
( while true; do sleep 60; echo "[heartbeat] capturespeed native build active $(date +%T)"; done ) &
heartbeat_pid=$!
trap 'kill "$heartbeat_pid" 2>/dev/null || true' EXIT
cd /mnt/c
/mnt/c/Windows/System32/cmd.exe /c "C:\vixen-native\capturespeed\reports\capturespeed\reproduce\native-build.bat"
