#!/usr/bin/env python3
"""Run a GPU ctest command and release the queue slot when it stalls (T-7061).

A lost Vulkan device leaves every later GPU case hanging until its own CTest timeout, which kept
two queued GPU suites for 104 and 60 minutes at almost no CPU.  This guard streams the ctest
output through unchanged and stops the whole process group when one of these holds:

  * a line reports a lost device (VK_ERROR_DEVICE_LOST, DXGI_ERROR_DEVICE_REMOVED, ...);
  * `--max-timeouts` cases in a row end in `***Timeout`;
  * ctest prints nothing for `--idle-seconds`.

On a trip it prints the first cause (what, which case, the log line, how to proceed) and exits 3.
Otherwise it exits with ctest's own status.

Usage: ctest-gpu-guard.py [--max-timeouts N] [--idle-seconds S] -- ctest ...
"""
from __future__ import annotations

import argparse
import os
import queue
import re
import signal
import subprocess
import sys
import threading
import time

TRIP_EXIT = 3
DEVICE_LOST = re.compile(r"VK_ERROR_DEVICE_LOST|DXGI_ERROR_DEVICE_(REMOVED|HUNG|RESET)|device (was )?lost", re.I)
CASE_RESULT = re.compile(r"^\s*\d+/\d+ Test\s+#\d+: (\S+) \.+\s*(\*\*\*|Passed)(\w[\w ]*)?")
START_LINE = re.compile(r"^\s*Start\s+\d+: (\S+)")
TIMEOUT_RESULT = re.compile(r"\*\*\*Timeout")


class Guard:
    def __init__(self, max_timeouts: int, idle_seconds: float):
        self.max_timeouts = max_timeouts
        self.idle_seconds = idle_seconds
        self.current_case = "<none started>"
        self.consecutive_timeouts = 0
        self.first_device_loss: tuple[str, str] | None = None
        self.timeout_cases: list[str] = []

    def observe(self, line: str) -> str | None:
        """Return a trip reason for this line, or None."""
        started = START_LINE.match(line)
        if started:
            self.current_case = started.group(1)
        if self.first_device_loss is None and DEVICE_LOST.search(line):
            self.first_device_loss = (self.current_case, line.strip())
            return (f"device loss while running '{self.current_case}': {line.strip()}")
        result = CASE_RESULT.match(line)
        if result:
            if TIMEOUT_RESULT.search(line):
                self.consecutive_timeouts += 1
                self.timeout_cases.append(result.group(1))
                if self.consecutive_timeouts >= self.max_timeouts:
                    return (f"{self.consecutive_timeouts} consecutive case timeouts "
                            f"({', '.join(self.timeout_cases[-self.consecutive_timeouts:])}); first: {line.strip()}")
            else:
                self.consecutive_timeouts = 0
        return None


def kill_group(process: subprocess.Popen) -> None:
    for sig in (signal.SIGTERM, signal.SIGKILL):
        try:
            os.killpg(process.pid, sig)
        except ProcessLookupError:
            return
        try:
            process.wait(timeout=10)
            return
        except subprocess.TimeoutExpired:
            continue


def run(arguments: list[str], guard: Guard) -> int:
    process = subprocess.Popen(arguments, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True, errors="replace", start_new_session=True)
    lines: queue.Queue[str | None] = queue.Queue()

    def pump() -> None:
        assert process.stdout is not None
        for text in process.stdout:
            lines.put(text)
        lines.put(None)

    threading.Thread(target=pump, daemon=True).start()
    last_output = time.monotonic()
    reason: str | None = None
    while reason is None:
        try:
            line = lines.get(timeout=1.0)
        except queue.Empty:
            if time.monotonic() - last_output >= guard.idle_seconds:
                reason = (f"no ctest output for {guard.idle_seconds:.0f}s while running "
                          f"'{guard.current_case}'")
            continue
        if line is None:
            return process.wait()
        last_output = time.monotonic()
        sys.stdout.write(line)
        sys.stdout.flush()
        reason = guard.observe(line)

    kill_group(process)
    print("\n[ctest-gpu-guard] STALLED GPU RUN STOPPED, queue slot released.\n"
          f"[ctest-gpu-guard] what: {reason}\n"
          f"[ctest-gpu-guard] where: case '{guard.current_case}' (see the ctest output above for the first error)\n"
          "[ctest-gpu-guard] why: a lost or hung GPU device makes every later case wait for its own timeout\n"
          "[ctest-gpu-guard] fix: rerun the named case alone through the queue; if the device is lost, restart the\n"
          "[ctest-gpu-guard]      GPU driver/WSL (wsl --shutdown) first. Tune with VIXEN_GPU_GUARD_MAX_TIMEOUTS /\n"
          "[ctest-gpu-guard]      VIXEN_GPU_GUARD_IDLE_SECONDS.", flush=True)
    return TRIP_EXIT


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--max-timeouts", type=int,
                        default=int(os.environ.get("VIXEN_GPU_GUARD_MAX_TIMEOUTS", "2")))
    parser.add_argument("--idle-seconds", type=float,
                        default=float(os.environ.get("VIXEN_GPU_GUARD_IDLE_SECONDS", "900")))
    parser.add_argument("command", nargs=argparse.REMAINDER)
    options = parser.parse_args()
    command = options.command[1:] if options.command[:1] == ["--"] else options.command
    if not command:
        print("[ctest-gpu-guard] no command given: pass `-- ctest ...` after the options", file=sys.stderr)
        return 2
    return run(command, Guard(options.max_timeouts, options.idle_seconds))


if __name__ == "__main__":
    sys.exit(main())
