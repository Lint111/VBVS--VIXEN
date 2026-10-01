#!/usr/bin/env python3
"""Run inside with-test-lock.sh; isolate ccache counters and retain raw evidence."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import threading
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--name", required=True)
parser.add_argument("--cache", required=True)
parser.add_argument("--output", default="build/vixcache-evidence")
parser.add_argument("command", nargs=argparse.REMAINDER)
args = parser.parse_args()
command = args.command[1:] if args.command[:1] == ["--"] else args.command
output = Path(args.output).resolve()
output.mkdir(parents=True, exist_ok=True)
env = dict(os.environ, CCACHE_DIR=str(Path(args.cache).resolve()), NINJA_STATUS="[%f/%t] ")
env["CCACHE_LOGFILE"] = str(output / f"{args.name}.ccache.log")
subprocess.run(["ccache", "-z"], env=env, check=True)
log = output / f"{args.name}.log"
start = time.monotonic()
commit = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
tracked = subprocess.check_output(["git", "ls-files"], text=True).splitlines()
logic_files = {Path(name) for name in tracked if name.endswith(".cmake") or Path(name).name == "CMakeLists.txt"}
logic_files.update(Path("VIXEN/cmake").glob("*.cmake"))
logic_hash = hashlib.sha256()
for path in sorted(logic_files):
    logic_hash.update(str(path).encode() + b"\0" + path.read_bytes() + b"\0")
working_tree = subprocess.check_output(["git", "status", "--porcelain"], text=True).splitlines()
finished = threading.Event()


def heartbeat():
    while not finished.wait(20):
        print(f"[measure] {args.name}: payload running {time.monotonic() - start:.0f}s; log={log}", flush=True)


thread = threading.Thread(target=heartbeat, daemon=True)
thread.start()
with log.open("w") as stream:
    result = subprocess.Popen(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    for line in result.stdout:
        stream.write(line)
        stream.flush()
        if re.match(r"^\[\d+/\d+\] ", line) or line.startswith(("FAILED:", "ninja:", "Error:", "STALE:")):
            print(line, end="", flush=True)
    result.wait()
finished.set()
thread.join()
elapsed = time.monotonic() - start
stats = subprocess.check_output(["ccache", "-s", "-v", "-v"], env=env, text=True)
(output / f"{args.name}.ccache.txt").write_text(stats)
counters = json.loads(subprocess.check_output(["ccache", "--print-stats", "--format=json"], env=env))
lines = log.read_text(errors="replace").splitlines()
units = [line for line in lines if re.match(r"^\[[1-9]\d*/\d+\] ", line)]
record = {
    "name": args.name,
    "commit": commit,
    "working_tree": working_tree,
    "build_logic_sha256": logic_hash.hexdigest(),
    "command": command,
    "environment": {key: value for key, value in env.items() if key.startswith("CCACHE_")},
    "wall_seconds": round(elapsed, 3),
    "exit_code": result.returncode,
    "ninja_units": len(units),
    "compile_units": sum("Building " in line and " object " in line for line in units),
    "link_units": sum("Linking " in line for line in units),
    "glob_checks": sum("Re-checking globbed directories" in line for line in lines),
    "ccache": counters,
    "log": str(log),
}
(output / f"{args.name}.json").write_text(json.dumps(record, indent=2) + "\n")
print(json.dumps({key: value for key, value in record.items() if key != "ccache"}, indent=2), flush=True)
print(stats, flush=True)
print("\n".join(lines[-30:]), flush=True)
raise SystemExit(result.returncode)
