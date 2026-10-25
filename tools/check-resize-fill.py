#!/usr/bin/env python3
"""Check that every probed window was filled by its swapchain.

Input: a run directory written by tools/vixen-multiwindow-probe.ps1 (probe.txt, procN.log, procN.err).
For each "<tag> procN ... client=WxH" line, the extent in force at that moment is, in order of
preference:
  1. the last "[RESIZE_TRACE] ... swapchain=WxH" line of procN.err (VIXEN_RESIZE_TRACE=1 builds), else
  2. the last "[GPUYIELD_CALIBRATION] ... extent=TxU" line of procN.log: the compute dispatch extent
     in 8-pixel tiles, i.e. ceil(width/8) x ceil(height/8) of the render target that follows the
     swapchain at render scale 1.
The window is filled when that extent equals the client size. A snapshot is also reported when the
process was not responding.

Usage: check-resize-fill.py <run-dir>
Exit 0 when every responding snapshot is filled, 1 otherwise.
"""
import datetime
import re
import sys
from pathlib import Path

PROBE = re.compile(r"^(\d\d:\d\d:\d\d\.\d+) (\S+) proc(\d+) pid=(\d+)(?: responding=(\w+))? client=(\d+)x(\d+)")
TILES = re.compile(r"^\[(\d\d:\d\d:\d\d\.\d+)\].*GPUYIELD_CALIBRATION.* extent=(\d+)x(\d+)")
TRACE = re.compile(r"\[RESIZE_TRACE\] t_ms=(\d+) where=\S+ framebuffer=(\d+)x(\d+) swapchain=(\d+)x(\d+)")


def seconds(stamp: str) -> float:
    h, m, s = stamp.split(":")
    return int(h) * 3600 + int(m) * 60 + float(s)


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) != 1:
        print(__doc__)
        return 2
    run = Path(args[0])
    tiles = {}
    traces = {}
    for log in sorted(run.glob("proc*.log")):
        index = int(re.sub(r"\D", "", log.stem))
        events = []
        for line in log.read_text(errors="replace").splitlines():
            match = TILES.match(line)
            if match:
                events.append((seconds(match.group(1)), int(match.group(2)), int(match.group(3))))
        tiles[index] = events
    for err in sorted(run.glob("proc*.err")):
        index = int(re.sub(r"\D", "", err.stem))
        events = []
        for line in err.read_text(errors="replace").splitlines():
            match = TRACE.search(line)
            if match:
                when = datetime.datetime.fromtimestamp(int(match.group(1)) / 1000.0)
                events.append((when.hour * 3600 + when.minute * 60 + when.second + when.microsecond / 1e6,
                               int(match.group(4)), int(match.group(5))))
        traces[index] = events

    failures = 0
    rows = 0
    for line in (run / "probe.txt").read_text(errors="replace").splitlines():
        match = PROBE.match(line)
        if not match:
            continue
        stamp, tag, proc, _pid, responding, cw, ch = match.groups()
        when = seconds(stamp)
        index = int(proc)
        client = (int(cw), int(ch))
        events = [e for e in traces.get(index, []) if e[0] <= when]
        if events:
            extent, expected, source = (events[-1][1], events[-1][2]), client, "trace"
        else:
            events = [e for e in tiles.get(index, []) if e[0] <= when]
            extent = (events[-1][1], events[-1][2]) if events else None
            expected, source = ((client[0] + 7) // 8, (client[1] + 7) // 8), "tiles"
        status = "FILLED" if extent == expected else "PARTIAL"
        if responding == "False":
            status = "NOT-RESPONDING"
        rows += 1
        if status != "FILLED":
            failures += 1
        print(f"{tag:14s} proc{proc} client={cw}x{ch} {source}-extent={'%dx%d' % extent if extent else 'n/a'} (want {expected[0]}x{expected[1]}) {status}")
    print(f"{rows} snapshots, {failures} not filled")
    return 0 if failures == 0 and rows else 1


if __name__ == "__main__":
    sys.exit(main())
