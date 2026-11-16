#!/usr/bin/env bash
# tools/node-width.sh keeps the generic direct WSL sizing rule. Queued native width is covered by
# Native.JobCount, which reads the queue grant instead of host-side claim files.
# Usage: test_node_width.sh <repo-root>
set -uo pipefail
repo_root="${1:?repo root}"
# shellcheck source=../../../tools/node-width.sh
source "$repo_root/tools/node-width.sh"
failures=0
expect() {
    local want="$1" got="$2" name="$3"
    if [[ "$got" == "$want" ]]; then echo "PASS $name"; else echo "FAIL $name: want $want got $got"; failures=$((failures + 1)); fi
}

expect 16 "$(nw_jobs 16 45149 1500)" "node width when memory allows"
expect 8 "$(nw_jobs 32 17408 2048)" "memory caps the direct width"
expect 1 "$(nw_jobs 32 500 10000)" "floor 1 when nothing fits"
expect 1 "$(nw_jobs 32 1024 900)" "floor 1 at exactly the headroom"

exit "$failures"
