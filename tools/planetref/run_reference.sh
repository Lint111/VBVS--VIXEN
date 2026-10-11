#!/usr/bin/env bash
# Regenerates match blocks (offline caches), renders every committed scene, and runs the disc-geometry witness.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
py="$here/.venv/bin/python"
cd "$here"
"$py" photo_match.py --offline
"$py" fixture_c1.py
"$py" mitsuba_export.py scenes/*.json
"$py" measure_disc.py c1_epic_20170821T151450 c1_epic_20170821T154450
