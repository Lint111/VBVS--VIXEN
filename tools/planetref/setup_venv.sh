#!/usr/bin/env bash
# Creates tools/planetref/.venv (git-ignored) with the Mitsuba 3.9.1 cp314 wheel (BSD-3) and the pinned numpy/scipy
# (unpinned versions rewrote the Lambert fit measurements in the witness JSON files, T-2869).
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
python3 -m venv "$here/.venv"
"$here/.venv/bin/pip" install -q -r "$here/requirements.txt"
"$here/.venv/bin/python" -c "import mitsuba; print('mitsuba', mitsuba.__version__)"
