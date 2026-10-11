set -euo pipefail
source .tmp/gpuyield/runtime-env.sh
bash .tmp/gpuyield-run2/targeted-tests.sh > reports/gpuyield-run2/targeted-contract-tests.log 2>&1
repo_root="$PWD"
out="$repo_root/reports/gpuyield-run2/editor-after"
mkdir -p "$out"
sha256sum build/gpuyield/binaries/vixen_editor build/gpuyield/binaries/VIXEN > "$out/binaries.sha256"
cd build/gpuyield/binaries
VIXEN_GPUYIELD_TRACE=1 VIXEN_EDITOR_TEST_CAMERA=top-down VIXEN_EXIT_AFTER_FRAMES=500 VIXEN_PERF_CSV="$out/vixen-perf.csv" python3 "$repo_root/tools/measure-gpu-cadence.py" --probe "$repo_root/.tmp/gpuyield-run2/gpu-cadence-probe" --spirv "$repo_root/.tmp/gpuyield/gpu-cadence-probe.spv" --output "$out" --seconds 90 --timeout 240 --command ./vixen_editor
cd "$repo_root"
python3 tools/summarize-gpu-yield.py --log "$out/vixen.log" --probe "$out/probe.csv" --output "$out/timing.json"
bash tools/run-vixen-windowed-captures.sh "$repo_root/build/gpuyield/binaries" VIXEN "$repo_root/build/gpuyield/binaries" vixen_editor "$repo_root/reports/gpuyield-run2/captures-after"
python3 tools/compare-capture-pixels.py reports/gpuyield-run2/captures-before reports/gpuyield-run2/captures-after --require-byte-identical
bash .tmp/gpuyield-run2/probe-native.sh > reports/gpuyield-run2/native-after.log 2>&1
python3 tools/summarize-gpu-yield.py --probe reports/gpuyield-run2/native-after.csv --output reports/gpuyield-run2/native-after.json
python3 .tmp/gpuyield-run2/native-overlap.py
bash /home/liory/.local/bin/with-test-lock.sh --agent gpuyield --resource light --label run2-final-22-checks -- bash .tmp/gpuyield-run2/refresh-checks.sh > reports/gpuyield-run2/verification/final-22-checks.log 2>&1
bash /home/liory/.local/bin/with-test-lock.sh --agent gpuyield --resource test --label run2-full-rendergraph-svo -- bash .tmp/gpuyield-run2/full-suite.sh > reports/gpuyield-run2/verification/full-suite.log 2>&1
bash /home/liory/.local/bin/with-test-lock.sh --agent gpuyield --resource build --label run2-final-noop -- cmake --build build/gpuyield --parallel 4 > reports/gpuyield-run2/verification/noop-build.log 2>&1
