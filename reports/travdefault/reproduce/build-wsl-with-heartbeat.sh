#!/usr/bin/env bash
set -euo pipefail

cmake --build ../build/wsl &
build_pid=$!
heartbeat_pid=

cleanup() {
  if [[ -n "$heartbeat_pid" ]]; then
    kill "$heartbeat_pid" 2>/dev/null || true
    wait "$heartbeat_pid" 2>/dev/null || true
  fi
}
trap cleanup EXIT

(
  while kill -0 "$build_pid" 2>/dev/null; do
    sleep 60
    if kill -0 "$build_pid" 2>/dev/null; then
      echo "[heartbeat] travdefault WSL build active $(date -Is)"
    fi
  done
) &
heartbeat_pid=$!

wait "$build_pid"
