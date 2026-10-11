set -euo pipefail
python3 - <<'PY'
from pathlib import Path
stamps=list(Path('build/gpuyield/codegen/checks').glob('*_check.stamp'))
assert len(stamps)==22, len(stamps)
for stamp in stamps:
    stamp.unlink()
PY
cmake --build build/gpuyield --target codegen/all vixen_editor --parallel 4
