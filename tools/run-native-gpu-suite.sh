#!/usr/bin/env bash
set -euo pipefail

# Queue one Windows-native serial GPU suite.  The native checkout and cmd.exe
# invocation are owned by vixen-native.sh; this wrapper owns only GPU admission.
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
lane="${VIXEN_NATIVE_LANE:-$(basename "$root")}"
native_root="${VIXEN_NATIVE_ROOT:-/mnt/c/vixen-native}"
system32="${VIXEN_NATIVE_SYSTEM32:-/mnt/c/Windows/System32}"
log="${VIXEN_NATIVE_SUITE_LOG:-$root/.tmp/vixen-native/$lane-suite-$(date +%Y%m%d-%H%M%S).log}"

exec bash /home/liory/.local/bin/with-test-lock.sh \
  --agent cx-gpuwin --resource gpu --label "cx-gpuwin:native-gpu-suite" -- \
  env VIXEN_NATIVE_ROOT="$native_root" VIXEN_NATIVE_SYSTEM32="$system32" \
    bash "$root/tools/vixen-native.sh" --lane "$lane" --log "$log" \
      bat tools/run-native-gpu-suite.bat "$@"
