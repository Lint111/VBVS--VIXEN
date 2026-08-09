#!/bin/bash
set -e
LOCKDIR=/home/liory/Github/undertow/tools
PERF=/mnt/c/GitHub/undertow-winbuild/perf/batch40-gates
SMI_LOG=$PERF/nvidia-smi-poll.log
: > "$SMI_LOG"

# background nvidia-smi poller (WSL-side, correlate by timestamp with per-leg logs)
( while true; do
    ts=$(date +%s.%N)
    line=$(nvidia-smi --query-gpu=clocks.sm,pstate,utilization.gpu,power.draw --format=csv,noheader,nounits 2>/dev/null || echo "ERR")
    echo "$ts,$line" >> "$SMI_LOG"
    sleep 2
  done ) &
SMI_PID=$!
echo "nvidia-smi poller pid=$SMI_PID"

cleanup() { kill $SMI_PID 2>/dev/null || true; }
trap cleanup EXIT

cd /mnt/c
bash "$LOCKDIR/with-test-lock.sh" --resource gpu --label streamA-batch40 \
  cmd.exe /c "C:\GitHub\undertow-winbuild\gates\batch40_sweep.bat"

echo "SWEEP DONE rc=$?"
