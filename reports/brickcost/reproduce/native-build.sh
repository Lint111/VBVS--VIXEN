#!/usr/bin/env bash
set -euo pipefail
git -C /mnt/c/vixen-native/brickcost fetch "$PWD" lane-brickcost
git -C /mnt/c/vixen-native/brickcost merge --ff-only FETCH_HEAD
( while true; do sleep 60; echo "[heartbeat] brickcost native build active $(date +%T)"; done ) &
heartbeat_pid=$!
trap 'kill "$heartbeat_pid" 2>/dev/null || true' EXIT
cd /mnt/c
/mnt/c/Windows/System32/cmd.exe /c "C:\vixen-native\brickcost\reports\brickcost\reproduce\native-build.bat"
