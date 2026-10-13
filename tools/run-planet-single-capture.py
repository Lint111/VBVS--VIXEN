#!/usr/bin/env python3
"""Run one planet capture state in a fresh process using its registered CTest environment."""

import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from planetcapture import ARTIFACT_SUFFIXES, GTEST_FILTER, registered_capture  # noqa: E402


def main() -> int:
    if len(sys.argv) != 4:
        raise SystemExit("usage: run-planet-single-capture.py <build-dir> <state> <output-dir>")
    build = Path(sys.argv[1]).resolve()
    state = sys.argv[2]
    output = Path(sys.argv[3]).resolve()
    output.mkdir(parents=True, exist_ok=True)

    executable, environment, cwd = registered_capture(build)
    environment.update(
        VIXEN_PLANET_CAPTURE_STATE=state,
        VIXEN_PLANET_CAPTURE_DIR=str(output),
        VIXEN_PLANET_READBACK="1",
    )
    # The APU is shared with other agents' jobs; a device-lost under contention is retried.
    for attempt in range(3):
        run = subprocess.run([executable, GTEST_FILTER], cwd=cwd, env=environment)
        if run.returncode == 0:
            break
        print(f"{state}: attempt {attempt + 1} exited {run.returncode}")
    else:
        raise SystemExit(f"{state}: capture failed after 3 attempts")

    for suffix in ARTIFACT_SUFFIXES:
        if not (output / f"{state}{suffix}").is_file():
            raise FileNotFoundError(output / f"{state}{suffix}")
    print(f"{state}: one fresh-process planet capture passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
