#!/usr/bin/env python3
"""Queued instrumentation-only baseline, checked against repeated source captures."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

build, state, output, reference = sys.argv[1:]
build, output, reference = map(lambda p: Path(p).resolve(), (build, output, reference))
description = json.loads(subprocess.check_output([
    "ctest", "--test-dir", str(build), "-R", "^Application.LookdevCapture\\.",
    "--show-only=json-v1"], text=True))
test = description["tests"][0]
properties = {p["name"]: p["value"] for p in test["properties"]}
environment = os.environ.copy()
for item in properties.get("ENVIRONMENT", []):
    name, value = item.split("=", 1)
    environment[name] = value
for item in properties.get("ENVIRONMENT_MODIFICATION", []):
    name, operation = item.split("=", 1)
    if operation == "unset:": environment.pop(name, None)
    else: raise ValueError(f"unsupported environment operation {item}")
output.mkdir(parents=True, exist_ok=True)
environment.update(VIXEN_LOOKDEV_CAPTURE_STATE=state,
    VIXEN_LOOKDEV_CAPTURE_DIR=str(output), VIXEN_LOOKDEV_READBACK="1",
    VIXEN_PERF_CSV=str(output/"gpu-timing.csv"))
subprocess.run([str(build/"application/main/vixen_lookdev_capture"),
    "--gtest_filter=LookdevCapture.RendersFourStatesAtFourAnglesDeterministically"],
    cwd=properties.get("WORKING_DIRECTORY", str(build)), env=environment, check=True)
for angle in range(4):
    name = f"{state}-angle-{angle}.png"
    if hashlib.sha256((output/name).read_bytes()).digest() != hashlib.sha256((reference/name).read_bytes()).digest():
        raise AssertionError(f"instrumentation changed baseline pixels: {name}")
print(f"{state}: all four instrumented captures match the repeated original baseline")
