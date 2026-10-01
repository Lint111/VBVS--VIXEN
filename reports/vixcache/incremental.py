#!/usr/bin/env python3
"""Queued companion to measure.py: no-op and one-leaf builds for both scopes."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--phase", required=True)
parser.add_argument("--build", required=True)
parser.add_argument("--cache", required=True)
args = parser.parse_args()
build = Path(args.build).resolve()
entries = json.loads((build / "compile_commands.json").read_text())
targets = sorted({entry["output"].split("/CMakeFiles/")[1].split(".dir/")[0]
                  for entry in entries
                  if any(part in entry["output"] for part in ["/RenderGraph/tests/", "/SVO/tests/"])
                  and entry["output"].split("/CMakeFiles/")[1].startswith("test_")})
source = Path(next(entry["file"] for entry in entries
                   if entry["file"].endswith("/RenderGraph/tests/test_ui_hit_mask.cpp")))
original = source.stat()
measure = Path(__file__).with_name("measure.py")
try:
    for action in ["noop", "leaf"]:
        for scope in ["full", "suites"]:
            if action == "leaf":
                source.touch()
            command = ["cmake", "--build", str(build), "--parallel", "4"]
            if scope == "suites":
                command += ["--target", *targets]
            subprocess.run([sys.executable, str(measure), "--name", f"{args.phase}-{action}-{scope}",
                            "--cache", args.cache, "--", *command], check=True)
finally:
    os.utime(source, ns=(original.st_atime_ns, original.st_mtime_ns))
