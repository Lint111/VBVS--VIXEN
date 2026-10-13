#!/usr/bin/env python3
"""Frame-count sweep for one planet capture state (R536, R565 C2 witness).

Captures the state once per frame count in a fresh process (VIXEN_PLANET_CAPTURE_FRAMES=N) and
hashes the PNG and the readback files of every count against the reference count (64). The default
capture is 8 frames (two full per-frame rings); this sweep is the evidence that 8 produces the same
bytes as 64 for the state. Run one state per GPU queue job:

  python3 tools/planet-capture-frame-sweep.py build/wsl C1 .tmp/sweep
  python3 tools/planet-capture-frame-sweep.py build/wsl C1 .tmp/sweep --frames 1,2,4,8,16,32,64

Writes <out>/<state>/f<N>/<state>.* per count, <out>/<state>/f<N>.log (the process output) and
<out>/<state>/sweep.tsv (state, frames, wall seconds, attempts, five hashes, identical-to-reference).
Exit status is 1 when the default count's bytes differ from the reference count's: that state keeps
the reference count.
"""

import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from planetcapture import ARTIFACT_SUFFIXES, GTEST_FILTER, registered_capture  # noqa: E402

DEFAULT_FRAMES = "1,2,4,8,16,32,64"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def capture(executable, environment, cwd, state, frames, output: Path, log: Path, attempts):
    """Run one fresh-process capture; return (wall seconds, attempts used). Raises on failure."""
    output.mkdir(parents=True, exist_ok=True)
    env = dict(environment)
    env.update(
        VIXEN_PLANET_CAPTURE_STATE=state,
        VIXEN_PLANET_CAPTURE_DIR=str(output),
        VIXEN_PLANET_READBACK="1",
        VIXEN_PLANET_CAPTURE_FRAMES=str(frames),
    )
    # The sweep measures; the per-process budget is a gate for the registered test, not for this run.
    env.setdefault("VIXEN_PLANET_CAPTURE_BUDGET_MS", "3600000")
    with open(log, "w") as handle:
        for attempt in range(1, attempts + 1):
            started = time.monotonic()
            run = subprocess.run([executable, GTEST_FILTER], cwd=cwd, env=env,
                                 stdout=handle, stderr=subprocess.STDOUT)
            wall = time.monotonic() - started
            if run.returncode == 0:
                return wall, attempt
            print(f"[sweep] {state} frames={frames}: attempt {attempt} exited {run.returncode} "
                  f"after {wall:.0f} s", flush=True)
    raise SystemExit(f"[sweep] {state} frames={frames}: capture failed after {attempts} attempts (log {log})")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("build_dir")
    ap.add_argument("state")
    ap.add_argument("out_dir")
    ap.add_argument("--frames", default=DEFAULT_FRAMES, help=f"comma list of frame counts (default {DEFAULT_FRAMES})")
    ap.add_argument("--reference", type=int, default=64, help="count whose bytes every other count must match")
    ap.add_argument("--default", type=int, default=8, help="the registered default count; the verdict is about it")
    ap.add_argument("--attempts", type=int, default=3, help="fresh-process retries per count (device loss under contention)")
    args = ap.parse_args()

    counts = [int(value) for value in args.frames.split(",") if value]
    if args.reference not in counts:
        counts.append(args.reference)
    state = args.state
    out = Path(args.out_dir).resolve() / state
    out.mkdir(parents=True, exist_ok=True)
    executable, environment, cwd = registered_capture(args.build_dir)

    rows = {}
    for frames in counts:
        print(f"[sweep] {state} frames={frames}: start", flush=True)
        wall, attempts = capture(executable, environment, cwd, state, frames,
                                 out / f"f{frames}", out / f"f{frames}.log", args.attempts)
        hashes = {}
        for suffix in ARTIFACT_SUFFIXES:
            path = out / f"f{frames}" / f"{state}{suffix}"
            if not path.is_file():
                raise SystemExit(f"[sweep] {state} frames={frames}: missing artefact {path}")
            hashes[suffix] = sha256(path)
        rows[frames] = {"wall": wall, "attempts": attempts, "hashes": hashes}
        print(f"[sweep] {state} frames={frames}: wall {wall:.1f} s, attempts {attempts}, "
              f"png {hashes['.png'][:16]} hits {hashes['.hits.bin'][:16]}", flush=True)

    reference = rows[args.reference]["hashes"]
    with open(out / "sweep.tsv", "w") as handle:
        handle.write("state\tframes\twall_s\tattempts\tpng\thits\tlighting\tmeter\tscene\tidentical_to_reference\n")
        for frames in sorted(rows):
            row = rows[frames]
            identical = all(row["hashes"][suffix] == reference[suffix] for suffix in ARTIFACT_SUFFIXES)
            row["identical"] = identical
            handle.write("\t".join([state, str(frames), f"{row['wall']:.1f}", str(row["attempts"])] +
                                   [row["hashes"][suffix] for suffix in ARTIFACT_SUFFIXES] +
                                   [str(identical).lower()]) + "\n")

    print(f"\n| state | frames | wall s (dzn, non-representative) | png sha256 | hits sha256 | identical to {args.reference} |")
    print("|---|---|---|---|---|---|")
    for frames in sorted(rows):
        row = rows[frames]
        differing = [suffix for suffix in ARTIFACT_SUFFIXES if row["hashes"][suffix] != reference[suffix]]
        print(f"| {state} | {frames} | {row['wall']:.1f} | {row['hashes']['.png'][:16]} | "
              f"{row['hashes']['.hits.bin'][:16]} | {'yes' if not differing else 'no: ' + ' '.join(differing)} |")

    verdict = rows[args.default]["identical"] if args.default in rows else False
    print(f"[SWEEP] state={state} default={args.default} reference={args.reference} "
          f"{'IDENTICAL' if verdict else 'DIFFERS'}", flush=True)
    return 0 if verdict else 1


if __name__ == "__main__":
    raise SystemExit(main())
