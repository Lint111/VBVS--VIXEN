set -euo pipefail
source .tmp/gpuyield/runtime-env.sh
out="$PWD/reports/gpuyield-run3/dozen-final"
mkdir -p "$out"
start=$(date +%s.%N)
VIXEN_RENDER_SCALE=1.0 VIXEN_WINDOW_WIDTH=500 VIXEN_WINDOW_HEIGHT=500 VIXEN_GPUYIELD_TRACE=1 VIXEN_EDITOR_TEST_CAMERA=top-down VIXEN_EXIT_AFTER_FRAMES=500 VIXEN_PERF_CSV="$out/vixen-perf.csv" timeout 240 build/gpuyield/binaries/vixen_editor > "$out/vixen.log" 2> "$out/host.log"
end=$(date +%s.%N)
python3 - "$start" "$end" "$out" <<'PY'
import json,sys
from pathlib import Path
Path(sys.argv[3], 'status.json').write_text(json.dumps({'exit':0,'frames':500,'scale':1.,'wall_seconds':float(sys.argv[2])-float(sys.argv[1])},indent=2)+'\n')
PY
