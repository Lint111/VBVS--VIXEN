#!/usr/bin/env bash
# Builds the RenderGraph CPU/compile test targets into an existing native build dir at the derived job count.
# Usage: build-rendergraph-tests.sh BINDIR_WSL
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; repo="$(cd "$here/../.." && pwd)"
export VIXEN_BINARY_DIR="$1"; unset VIXEN_MAX_BUILD_JOBS
mkdir -p "$here/rgtests"
for t in test_exposure_meter_mirror test_spatial_reuse_history_mirror test_array_type_validation test_field_extraction \
         test_resource_gatherer test_compile_time_resource_system test_recursive_validation; do
  echo "=== target $t"
  nice -n 10 bash "$repo/tools/vixen-native.sh" --log "$here/rgtests/build-$t.log" build build vixen-ninja "$t" 2>&1 | grep -E "build jobs|lock skipped|BUILD SUMMARY|All targets|FAILED|error|ERROR" || true
  grep -q "All targets built successfully" "$here/rgtests/build-$t.log" || { echo "BUILD FAILED: $t"; exit 1; }
done
