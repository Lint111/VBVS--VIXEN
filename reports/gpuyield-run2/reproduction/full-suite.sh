set -euo pipefail
if [[ -f .tmp/gpuyield-run2/checkpoint-stop-before-suite ]]; then
    printf '%s\n' 'CHECKPOINT: full ShaderManagement and RenderGraph/SVO suites were not started; resume without the lane checkpoint marker.'
    exit 124
fi
mkdir -p reports/gpuyield-run2/verification
# ShaderManagement tests are unlabelled; select their actual CTest directory.
env -u DISPLAY -u WAYLAND_DISPLAY -u VK_ICD_FILENAMES -u LD_LIBRARY_PATH \
    ctest --test-dir build/gpuyield/libraries/ShaderManagement --output-on-failure --parallel 1 \
    > reports/gpuyield-run2/verification/shader-management-suite.log 2>&1
if env -u DISPLAY -u WAYLAND_DISPLAY -u VK_ICD_FILENAMES -u LD_LIBRARY_PATH VIXEN_RIMLEAK_BENCHMARK=merged \
    ctest --test-dir build/gpuyield -L 'RenderGraph|SVO' --output-on-failure --parallel 1 \
    > .tmp/gpuyield-run2/full-suite-payload.log 2>&1; then
    suite_status=0
else
    suite_status=$?
fi
cp .tmp/gpuyield-run2/full-suite-payload.log reports/gpuyield-run2/verification/post-wave-suite.log
python3 - "$suite_status" <<'PY'
import json
from pathlib import Path
import re
import sys

status = int(sys.argv[1])
failed_path = Path('build/gpuyield/Testing/Temporary/LastTestsFailed.log')
failed = failed_path.read_text().splitlines() if failed_path.exists() else []
log = Path('.tmp/gpuyield-run2/full-suite-payload.log').read_text()
known = 'RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes'
allowed = (status == 8 and len(failed) == 1
           and re.fullmatch(r'\d+:' + re.escape(known), failed[0])
           and 'M4d_Output_IsPassthrough: recipe gradient capability mismatch: 94' in log)
result = {'ctest_exit_status': status, 'failing_tests': failed,
          'matches_permitted_opcode_94_only': bool(allowed)}
Path('reports/gpuyield-run2/verification/post-wave-suite-status.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
if status != 0 and not allowed:
    raise SystemExit('Unaccepted full-suite failure; inspect the retained log before further witnesses.')

detail = Path('build/gpuyield/Testing/Temporary/LastTest.log').read_text(errors='replace')
oracle = [line for line in detail.splitlines() if '[GPUYIELD_TILES]' in line or '[RIMLEAK_COST]' in line]
Path('reports/gpuyield-run2/verification/post-wave-gpu-oracle.log').write_text('\n'.join(oracle) + '\n')
PY
mkdir -p reports/gpuyield-run2/fixtures-after
cp -a build/gpuyield/runtime-captures/editor reports/gpuyield-run2/fixtures-after/editor
cp -a build/gpuyield/runtime-captures/hud reports/gpuyield-run2/fixtures-after/hud
python3 tools/compare-capture-pixels.py reports/gpuyield-evidence/baseline/fixtures reports/gpuyield-run2/fixtures-after --require-byte-identical
