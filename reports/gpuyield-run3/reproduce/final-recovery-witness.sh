set -u
set +e
bash /home/liory/.local/bin/with-test-lock.sh --status > reports/gpuyield-run3/shared-host-witness-queue-status.txt
bash .tmp/gpuyield-run3/native-recovery-matrix.sh
native_rc=$?
bash .tmp/gpuyield-run3/dozen-final.sh
dozen_rc=$?
env -u DISPLAY -u WAYLAND_DISPLAY -u VK_ICD_FILENAMES -u LD_LIBRARY_PATH ctest --test-dir build/gpuyield -R '^(GpuChunkCost\.|SubmitRecord\.|MultiDispatchIntegration.ActualNode)' --output-on-failure --parallel 1 > reports/gpuyield-run3/recovered-cost-and-submit-tests.log 2>&1
cost_rc=$?
python3 tools/test-summarize-gpu-yield.py > reports/gpuyield-run3/timestamp-validity-regressions.log 2>&1
metric_rc=$?
bash .tmp/gpuyield-run3/full-suite.sh
suite_rc=$?
python3 - "$native_rc" "$dozen_rc" "$cost_rc" "$suite_rc" "$metric_rc" <<'PY2'
import json,sys
from pathlib import Path
r=dict(zip(['native_matrix_exit','dozen_500_exit','cost_and_submit_exit','full_suite_exit','timestamp_validity_exit'],map(int,sys.argv[1:])))
Path('reports/gpuyield-run3/recovery-witness-status.json').write_text(json.dumps(r,indent=2)+'\n')
print(r)
raise SystemExit(any(r.values()))
PY2
