#!/usr/bin/env python3
"""Print the contention-sensitive values of a CalibrationStore JSON file.

Usage: summarize-calibration.py <file.json> [...]
Prints the file's provenance fields (contendedRun, overlappingRuns, timestamp) and, for each profile
that carries a chunk calibration, the per-extent {anchorUnits, unitNs, fixedNs} plus the profile's
lastMeasuredCostNs. Use it to compare the file before and after concurrent VIXEN runs.
"""
import json
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    for name in sys.argv[1:]:
        j = json.loads(Path(name).read_text())
        head = {k: j.get(k) for k in ("timestamp", "contendedRun", "overlappingRuns")}
        print(f"{name}: {head}")
        for p in j.get("profiles", []):
            chunks = p.get("chunkCalibration") or {}
            for extent, c in sorted(chunks.items()):
                print(f"  {p['name']} extent={extent} anchorUnits={c['anchorUnits']} "
                      f"unitNs={c['unitNs']:.1f} fixedNs={c['fixedNs']:.0f} "
                      f"lastMeasuredCostNs={p.get('lastMeasuredCostNs')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
