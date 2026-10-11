set -euo pipefail
mkdir -p reports/gpuyield-run3/verification-final
# ShaderManagement tests are unlabelled; select their actual CTest directory.
env -u DISPLAY -u WAYLAND_DISPLAY -u VK_ICD_FILENAMES -u LD_LIBRARY_PATH \
    ctest --test-dir build/gpuyield/libraries/ShaderManagement --output-on-failure --parallel 1 \
    > reports/gpuyield-run3/verification-final/shader-management-suite.log 2>&1
python3 - <<'PY_FRESH'
from pathlib import Path
import time
root=Path('build/gpuyield/runtime-captures')
for category in ('editor', 'hud'):
    for image in (root/category).glob('*.png'):
        image.unlink()
Path('reports/gpuyield-run3/verification-final/capture-start-ns.txt').write_text(str(time.time_ns())+'\n')
PY_FRESH
if env -u DISPLAY -u WAYLAND_DISPLAY -u VK_ICD_FILENAMES -u LD_LIBRARY_PATH VIXEN_RIMLEAK_BENCHMARK=merged \
    ctest --test-dir build/gpuyield -L 'RenderGraph|SVO' --output-on-failure --parallel 1 \
    > .tmp/gpuyield-run3/full-suite-payload.log 2>&1; then
    suite_status=0
else
    suite_status=$?
fi
cp .tmp/gpuyield-run3/full-suite-payload.log reports/gpuyield-run3/verification-final/post-wave-suite.log
python3 - "$suite_status" <<'PY'
import json
from pathlib import Path
import re
import sys

status = int(sys.argv[1])
failed_path = Path('build/gpuyield/Testing/Temporary/LastTestsFailed.log')
failed = failed_path.read_text().splitlines() if failed_path.exists() else []
log = Path('.tmp/gpuyield-run3/full-suite-payload.log').read_text()
known = 'RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes'
allowed = (status == 8 and len(failed) == 1
           and re.fullmatch(r'\d+:' + re.escape(known), failed[0])
           and 'M4d_Output_IsPassthrough: recipe gradient capability mismatch: 94' in log)
result = {'ctest_exit_status': status, 'failing_tests': failed,
          'matches_permitted_opcode_94_only': bool(allowed)}
Path('reports/gpuyield-run3/verification-final/post-wave-suite-status.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
if status != 0 and not allowed:
    raise SystemExit('Unaccepted full-suite failure; inspect the retained log before further witnesses.')

detail = Path('build/gpuyield/Testing/Temporary/LastTest.log').read_text(errors='replace')
oracle = [line for line in detail.splitlines() if '[GPUYIELD_TILES]' in line or '[RIMLEAK_COST]' in line]
Path('reports/gpuyield-run3/verification-final/post-wave-gpu-oracle.log').write_text('\n'.join(oracle) + '\n')
PY
python3 - <<'PY_FRESH'
from pathlib import Path
import hashlib,json
start=int(Path('reports/gpuyield-run3/verification-final/capture-start-ns.txt').read_text())
root=Path('build/gpuyield/runtime-captures')
images=[* (root/'editor').glob('*.png'), * (root/'hud').glob('*.png')]
assert len(images)==9, f'Expected 9 fresh captures, got {len(images)}'
assert all(p.stat().st_mtime_ns>=start for p in images), 'Stale capture found after successful producer'
Path('reports/gpuyield-run3/verification-final/capture-provenance.json').write_text(json.dumps([{'path':str(p),'mtime_ns':p.stat().st_mtime_ns,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in images],indent=2)+'\n')
PY_FRESH
mkdir -p reports/gpuyield-run3/fixtures-after
cp -a build/gpuyield/runtime-captures/editor reports/gpuyield-run3/fixtures-after/editor
cp -a build/gpuyield/runtime-captures/hud reports/gpuyield-run3/fixtures-after/hud
python3 tools/compare-capture-pixels.py reports/gpuyield-evidence/baseline/fixtures reports/gpuyield-run3/fixtures-after --require-byte-identical
