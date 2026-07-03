#!/usr/bin/env bash
# Empirical zero-footprint proof (spec §8): a VIXEN_FAIL_SCENARIOS=OFF build's artifacts must
# contain NO scenario/injector/seam symbols; an ON build MUST contain them (proves the probe works —
# an empty grep on both sides would be a broken probe, not a passing gate).
set -e
OFF_DIR="${1:-build-wsl-off}"; ON_DIR="${2:-build-wsl}"
SYMS='ScenarioRegistry|FaultInjector|InjectWindowEvent|ReplayScenarioRegistrars|s_vixen_fail_scenarios_'
find_syms() { find "$1" \( -name 'libRenderGraph*.a' -o -name 'libRenderGraphNodes*.a' -o -name 'libVixenApp*.a' \) \
              -exec nm -C {} + 2>/dev/null | grep -cE "$SYMS" || true; }
OFF_COUNT=$(find_syms "$OFF_DIR"); ON_COUNT=$(find_syms "$ON_DIR")
echo "OFF-build scenario symbols: $OFF_COUNT (must be 0)"
echo "ON-build  scenario symbols: $ON_COUNT (must be > 0)"
[ "$OFF_COUNT" -eq 0 ] && [ "$ON_COUNT" -gt 0 ] && echo "COMPILE-OUT PROOF: PASS" || { echo "FAIL"; exit 1; }
