"""Shared helpers for running the planet capture executable outside CTest.

The planet capture tests (Application.PlanetCapture.<state>, and the native-only aggregate) register
the executable and the environment the capture needs; these helpers read them back from CTest so a
queue job or a sweep runs the same binary under the same environment as the registered test.
"""

import json
import os
from pathlib import Path
import subprocess

GTEST_FILTER = "--gtest_filter=PlanetCapture.RendersStateDeterministically"
TEST_PREFIX = "Application.PlanetCapture."


def registered_capture(build_dir):
    """Return (executable, environment, working_directory) of the registered planet capture."""
    build = Path(build_dir).resolve()
    metadata = json.loads(subprocess.check_output([
        "ctest", "--test-dir", str(build), "-R", "^Application\\.PlanetCapture\\.",
        "--show-only=json-v1",
    ], text=True))
    test = next(item for item in metadata["tests"]
                if item["name"].startswith(TEST_PREFIX)
                and any(value.startswith("-DPLANET_CAPTURE_EXE=") for value in item.get("command", [])))
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
            current = environment.get(name)
            environment[name] = prefix if not current else f"{prefix}{os.pathsep}{current}"
        else:
            raise ValueError(f"unsupported CTest environment operation: {entry}")
    executable = next(value.split("=", 1)[1] for value in test["command"]
                      if value.startswith("-DPLANET_CAPTURE_EXE="))
    return executable, environment, properties.get("WORKING_DIRECTORY", str(build))


ARTIFACT_SUFFIXES = (".png", ".hits.bin", ".lighting.bin", ".meter.bin", ".scene.json")
