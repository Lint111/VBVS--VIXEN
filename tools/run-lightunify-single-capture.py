#!/usr/bin/env python3
"""Run one four-angle look-dev capture process using its registered CTest environment."""

import json
import os
from pathlib import Path
import subprocess
import sys


def main() -> int:
    if len(sys.argv) != 4:
        raise SystemExit("usage: run-lightunify-single-capture.py <build-dir> <state> <output-dir>")
    build = Path(sys.argv[1]).resolve()
    state = sys.argv[2]
    output = Path(sys.argv[3]).resolve()
    output.mkdir(parents=True, exist_ok=True)

    metadata = json.loads(subprocess.check_output([
        "ctest", "--test-dir", str(build), "-R", "^Application.LookdevCapture\\.",
        "--show-only=json-v1",
    ], text=True))
    test = next(item for item in metadata["tests"]
                if item["name"] == "Application.LookdevCapture.RendersFourStatesAtFourAnglesDeterministically")
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

    command = test["command"]
    executable = next(value.split("=", 1)[1] for value in command
                      if value.startswith("-DLOOKDEV_CAPTURE_EXE="))
    environment.update(
        VIXEN_LOOKDEV_CAPTURE_STATE=state,
        VIXEN_LOOKDEV_CAPTURE_DIR=str(output),
        VIXEN_LOOKDEV_READBACK="1",
        VIXEN_PERF_CSV=str(output / "gpu-timing.csv"),
    )
    subprocess.run([
        executable,
        "--gtest_filter=LookdevCapture.RendersFourStatesAtFourAnglesDeterministically",
    ], cwd=properties.get("WORKING_DIRECTORY", str(build)), env=environment, check=True)

    for angle in range(4):
        stem = output / f"{state}-angle-{angle}"
        for suffix in (".png", ".hits.bin", ".lighting.bin", ".meter.bin", ".scene.json"):
            if not stem.with_suffix(suffix).is_file():
                raise FileNotFoundError(stem.with_suffix(suffix))
    print(f"{state}: one fresh-process four-angle capture and timestamp sample set passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
