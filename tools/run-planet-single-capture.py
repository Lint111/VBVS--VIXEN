#!/usr/bin/env python3
"""Run one planet capture state in a fresh process using its registered CTest environment."""

import json
import os
from pathlib import Path
import subprocess
import sys

TEST = "Application.PlanetCapture.RendersStatesDeterministically"


def main() -> int:
    if len(sys.argv) != 4:
        raise SystemExit("usage: run-planet-single-capture.py <build-dir> <state> <output-dir>")
    build = Path(sys.argv[1]).resolve()
    state = sys.argv[2]
    output = Path(sys.argv[3]).resolve()
    output.mkdir(parents=True, exist_ok=True)

    metadata = json.loads(subprocess.check_output([
        "ctest", "--test-dir", str(build), "-R", "^Application.PlanetCapture\\.",
        "--show-only=json-v1",
    ], text=True))
    test = next(item for item in metadata["tests"] if item["name"] == TEST)
    properties = {item["name"]: item["value"] for item in test["properties"]}
    environment = os.environ.copy()
    for entry in properties.get("ENVIRONMENT", []):
        name, value = entry.split("=", 1)
        environment[name] = value
    for entry in properties.get("ENVIRONMENT_MODIFICATION", []):
        name, operation = entry.split("=", 1)
        if operation == "unset:":
            environment.pop(name, None)
        elif operation.startswith("set:"):
            environment[name] = operation.removeprefix("set:")
        elif operation.startswith("path_list_prepend:"):
            prefix = operation.removeprefix("path_list_prepend:")
            inherited = environment.get(name)
            environment[name] = prefix if not inherited else f"{prefix}{os.pathsep}{inherited}"
        else:
            raise ValueError(f"unsupported CTest environment operation: {entry}")

    executable = next(value.split("=", 1)[1] for value in test["command"]
                      if value.startswith("-DPLANET_CAPTURE_EXE="))
    environment.update(
        VIXEN_PLANET_CAPTURE_STATE=state,
        VIXEN_PLANET_CAPTURE_DIR=str(output),
        VIXEN_PLANET_READBACK="1",
    )
    # The APU is shared with other agents' jobs; a device-lost under contention is retried.
    for attempt in range(3):
        run = subprocess.run([executable, "--gtest_filter=PlanetCapture.RendersStateDeterministically"],
                             cwd=properties.get("WORKING_DIRECTORY", str(build)), env=environment)
        if run.returncode == 0:
            break
        print(f"{state}: attempt {attempt + 1} exited {run.returncode}")
    else:
        raise SystemExit(f"{state}: capture failed after 3 attempts")

    for suffix in (".png", ".hits.bin", ".lighting.bin", ".meter.bin", ".scene.json"):
        if not (output / f"{state}{suffix}").is_file():
            raise FileNotFoundError(output / f"{state}{suffix}")
    print(f"{state}: one fresh-process planet capture passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
