set -uo pipefail
out=reports/gpuyield-run5/verification
mkdir -p "$out" .tmp/gpuyield-run5
env -u DISPLAY -u WAYLAND_DISPLAY -u VK_ICD_FILENAMES -u LD_LIBRARY_PATH \
    ctest --test-dir build/gpuyield/libraries/ShaderManagement --output-on-failure --parallel 1 \
    > "$out/shader-management-suite.log" 2>&1
echo "ShaderManagement exit: $?"
python3 - <<'PY_FRESH'
from pathlib import Path
import time
root=Path('build/gpuyield/runtime-captures')
for category in ('editor', 'hud'):
    for image in (root/category).glob('*.png'):
        image.unlink()
Path('reports/gpuyield-run5/verification/capture-start-ns.txt').write_text(str(time.time_ns())+'\n')
PY_FRESH
env -u DISPLAY -u WAYLAND_DISPLAY -u VK_ICD_FILENAMES -u LD_LIBRARY_PATH VIXEN_RIMLEAK_BENCHMARK=merged \
    ctest --test-dir build/gpuyield -L 'RenderGraph|SVO' --output-on-failure --parallel 1 \
    > "$out/post-fix-suite.log" 2>&1
suite_status=$?
echo "Full suite exit: $suite_status"
cp build/gpuyield/runtime-captures/editor/run_editor_script.log "$out/run_editor_script.log" 2>/dev/null
python3 - "$suite_status" <<'PY'
import json,re,sys
from pathlib import Path
status=int(sys.argv[1])
failed_path=Path('build/gpuyield/Testing/Temporary/LastTestsFailed.log')
failed=failed_path.read_text().splitlines() if failed_path.exists() else []
log=Path('reports/gpuyield-run5/verification/post-fix-suite.log').read_text(errors='replace')
known='RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes'
r6='EditorToggleUndoCapture.GpuSubmissionsRespectDeclaredBudget'
names=[re.sub(r'^\d+:','',f) for f in failed]
result={'ctest_exit_status':status,'failing_tests':failed,
        'opcode_94_signature_present':'M4d_Output_IsPassthrough: recipe gradient capability mismatch: 94' in log,
        'only_opcode94_and_r6_dozen_timing':set(names)<= {known,r6},
        'r6_maxima':re.findall(r'actual: ([0-9.]+) vs 2',log)}
Path('reports/gpuyield-run5/verification/post-fix-suite-status.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
PY
python3 - <<'PY_FRESH'
from pathlib import Path
import hashlib,json
start=int(Path('reports/gpuyield-run5/verification/capture-start-ns.txt').read_text())
root=Path('build/gpuyield/runtime-captures')
images=[*(root/'editor').glob('*.png'),*(root/'hud').glob('*.png')]
fresh=[p for p in images if p.stat().st_mtime_ns>=start]
print('fresh captures:',len(fresh),'of',len(images))
Path('reports/gpuyield-run5/verification/capture-provenance.json').write_text(json.dumps([{'path':str(p),'mtime_ns':p.stat().st_mtime_ns,'fresh':p.stat().st_mtime_ns>=start,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in images],indent=2)+'\n')
PY_FRESH
rm -rf "$out/fixtures-after"; mkdir -p "$out/fixtures-after"
cp -a build/gpuyield/runtime-captures/editor "$out/fixtures-after/editor"
cp -a build/gpuyield/runtime-captures/hud "$out/fixtures-after/hud"
python3 tools/compare-capture-pixels.py reports/gpuyield-evidence/baseline/fixtures "$out/fixtures-after" --require-byte-identical > "$out/png-comparison.txt" 2>&1
echo "PNG comparison exit: $?"
cat "$out/png-comparison.txt"
sha256sum build/gpuyield/binaries/VIXEN build/gpuyield/binaries/vixen_editor > "$out/binaries.sha256"
