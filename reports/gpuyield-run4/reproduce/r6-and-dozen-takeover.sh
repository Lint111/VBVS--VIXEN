set -uo pipefail
out=reports/gpuyield-run4/takeover
mkdir -p "$out"
# 1. Live R6 gate on Dozen: the editor capture producer fixture is added by ctest automatically.
env -u DISPLAY -u WAYLAND_DISPLAY -u VK_ICD_FILENAMES -u LD_LIBRARY_PATH \
  ctest --test-dir build/gpuyield -R 'EditorToggleUndoCapture\.GpuSubmissionsRespectDeclaredBudget' --output-on-failure --parallel 1 \
  > "$out/r6-dozen.log" 2>&1
r6=$?
cp build/gpuyield/runtime-captures/editor/run_editor_script.log "$out/r6-run_editor_script.log" 2>/dev/null
echo "R6 exit: $r6"
# 2. 500-frame Dozen producer at scale 1.0 with the chunk trace.
source .tmp/gpuyield/runtime-env.sh
dz="$PWD/$out/dozen-takeover"
mkdir -p "$dz"
start=$(date +%s.%N)
VIXEN_RENDER_SCALE=1.0 VIXEN_WINDOW_WIDTH=500 VIXEN_WINDOW_HEIGHT=500 VIXEN_GPUYIELD_TRACE=1 VIXEN_EDITOR_TEST_CAMERA=top-down VIXEN_EXIT_AFTER_FRAMES=500 VIXEN_PERF_CSV="$dz/vixen-perf.csv" timeout 240 build/gpuyield/binaries/vixen_editor > "$dz/vixen.log" 2> "$dz/host.log"
rc=$?
end=$(date +%s.%N)
python3 - "$start" "$end" "$dz" "$rc" <<'PY'
import json,sys
from pathlib import Path
Path(sys.argv[3], 'status.json').write_text(json.dumps({'exit':int(sys.argv[4]),'requested_frames':500,'scale':1.,'wall_seconds':float(sys.argv[2])-float(sys.argv[1])},indent=2)+'\n')
PY
echo "Dozen producer exit: $rc"
sha256sum build/gpuyield/binaries/VIXEN build/gpuyield/binaries/vixen_editor > "$out/binaries.sha256"
[ "$r6" -eq 0 ] && [ "$rc" -eq 0 ]
