#!/usr/bin/env bash
# Run one GoogleTest binary under a --gtest_filter and FAIL when the filter selects nothing.
#
# GoogleTest 1.14 (the pinned version) has no --gtest_fail_if_no_test_selected: a filter that matches
# zero cases, or a binary that is stale / mid-rebuild and lacks the case, exits 0 after "Running 0 tests"
# (T-4154: RayGenerationTileContract.* passed with zero tests against an older executable). This wrapper
# lists the selection first (--gtest_list_tests honours the same filter) and checks it.
#
# usage: run-gtest-filtered.sh [--require CASE]... <test-binary> <gtest-filter> [extra gtest args...]
#   --require CASE   a full "Suite.Case" name that must be among the selected cases (repeatable)
# exit: 2 usage / unusable binary, 3 zero tests selected, 4 a required case is missing, else the binary's exit code.
# Run it as the payload of a queued job (with-test-lock.sh ... -- tools/run-gtest-filtered.sh ...); it does not queue itself.
set -euo pipefail

required=()
while [[ "${1:-}" == --require ]]; do
    [[ $# -ge 2 ]] || { echo "run-gtest-filtered: --require needs a Suite.Case name" >&2; exit 2; }
    required+=("$2"); shift 2
done
if [[ $# -lt 2 ]]; then
    echo "run-gtest-filtered: usage: $0 [--require Suite.Case]... <test-binary> <gtest-filter> [gtest args...]" >&2
    exit 2
fi
binary="$1"; filter="$2"; shift 2
if [[ ! -x "$binary" ]]; then
    echo "run-gtest-filtered: '$binary' is not an executable file; build the test target first (a missing or mid-rebuild binary must not be run)." >&2
    exit 2
fi

listing="$("$binary" --gtest_list_tests "--gtest_filter=$filter" < /dev/null)" || {
    echo "run-gtest-filtered: '$binary --gtest_list_tests' failed; cannot verify the selection for filter '$filter'." >&2
    exit 2
}
# --gtest_list_tests prints "Suite." lines followed by "  Case" lines (with optional "# GetParam()" comments).
selected="$(awk '/^[^ ]/ { suite = $1; next } NF { print suite $1 }' <<<"$listing")"
count=0
[[ -n "$selected" ]] && count="$(wc -l <<<"$selected")"
if [[ "$count" -eq 0 ]]; then
    echo "run-gtest-filtered: filter '$filter' selects 0 tests in '$binary'. The binary may be stale or still rebuilding, or the filter is misspelled; rebuild the target and check --gtest_list_tests." >&2
    exit 3
fi
for name in "${required[@]}"; do
    if ! grep -Fxq -- "$name" <<<"$selected"; then
        echo "run-gtest-filtered: required case '$name' is not selected by filter '$filter' in '$binary' ($count cases selected). The binary is probably older than the source; rebuild the target." >&2
        exit 4
    fi
done
echo "run-gtest-filtered: $count test(s) selected by '$filter'"
exec "$binary" "--gtest_filter=$filter" "$@"
