#!/usr/bin/env python3
"""Queued CTest witness: select suites from fresh executable ownership."""
import argparse
import json
from pathlib import Path
import subprocess
import threading
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build", required=True)
parser.add_argument("--name", required=True)
args = parser.parse_args()
build = Path(args.build).resolve()
output = Path("build/vixcache-evidence").resolve()
output.mkdir(parents=True, exist_ok=True)
inventory = subprocess.check_output(["ctest", "--test-dir", str(build), "--show-only=json-v1"], text=True)
(output / f"{args.name}.inventory.json").write_text(inventory)
tests = json.loads(inventory)["tests"]
selected = [test["name"] for test in tests if test.get("command") and
            (any(part in test["command"][0] for part in ["/libraries/RenderGraph/", "/libraries/SVO/"])
             or "test_headless_cornell_graph" in test["command"][0]
             or "test_headless_ui_graph" in test["command"][0])]
names = output / f"{args.name}.names.txt"
names.write_text("\n".join(selected) + "\n")
assert selected, "empty RenderGraph/SVO selection"
command = ["ctest", "--test-dir", str(build), "--tests-from-file", str(names), "--output-on-failure", "-j1"]
print(f"[witness] selected {len(selected)} RenderGraph/SVO/headless cases", flush=True)
start = time.monotonic()
finished = threading.Event()


def heartbeat():
    while not finished.wait(20):
        print(f"[witness] {args.name} running {time.monotonic() - start:.0f}s", flush=True)


thread = threading.Thread(target=heartbeat, daemon=True)
thread.start()
log = output / f"{args.name}.log"
with log.open("w") as stream:
    result = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    for line in result.stdout:
        stream.write(line)
        stream.flush()
        if any(word in line for word in ["***Failed", "***Exception", "tests passed", "Total Test time", "SKIPPED", "Not Run"]):
            print(line, end="", flush=True)
    result.wait()
finished.set()
thread.join()
record = {"command": command, "selected": len(selected), "exit_code": result.returncode,
          "wall_seconds": round(time.monotonic() - start, 3), "log": str(log)}
(output / f"{args.name}.json").write_text(json.dumps(record, indent=2) + "\n")
print(json.dumps(record, indent=2), flush=True)
print("\n".join(log.read_text(errors="replace").splitlines()[-30:]), flush=True)
raise SystemExit(result.returncode)
