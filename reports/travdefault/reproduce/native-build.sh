#!/usr/bin/env bash
set -euo pipefail

native_root=/mnt/c/vixen-native/travdefault
mkdir -p /mnt/c/vixen-native
repo_root=$(git rev-parse --show-toplevel)

(
  while true; do
    sleep 60
    echo "[heartbeat] travdefault native build active $(date +%T)"
  done
) &
heartbeat_pid=$!
trap 'kill "$heartbeat_pid" 2>/dev/null || true' EXIT

if [[ ! -d "$native_root/.git" ]]; then
  if [[ -e "$native_root" ]]; then
    echo "Native checkout exists without .git metadata: $native_root" >&2
    exit 1
  fi
  git clone --no-hardlinks --branch lane-travdefault "$repo_root" "$native_root"
fi

git -C "$native_root" fetch "$repo_root" lane-travdefault
git -C "$native_root" merge --ff-only FETCH_HEAD
cd /mnt/c
/mnt/c/Windows/System32/cmd.exe /c "C:\vixen-native\travdefault\reports\travdefault\reproduce\native-build.bat"
