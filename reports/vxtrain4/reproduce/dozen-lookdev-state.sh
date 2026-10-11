#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 midday|overcast|night-service" >&2
  exit 2
fi
state=$1
case "$state" in
  midday|overcast|night-service) ;;
  *) echo "usage: $0 midday|overcast|night-service" >&2; exit 2 ;;
esac

(
  while true; do
    sleep 60
    echo "[heartbeat] vxtrain4 Dozen look-dev state=$state active $(date +%T)"
  done
) &
heartbeat_pid=$!
trap 'kill "$heartbeat_pid" 2>/dev/null || true' EXIT

export VIXEN_LOOKDEV_CAPTURE_STATE="$state"
export VIXEN_LOOKDEV_CAPTURE_FRAMES=64
export VIXEN_LOOKDEV_READBACK=1
ctest --test-dir build/wsl \
  -R '^Application\.LookdevCapture\.RendersFourStatesAtFourAnglesDeterministically$' \
  --output-on-failure --parallel 1
