#!/usr/bin/env python3
"""Run the R477 second-client probe, optionally alongside a bounded VIXEN command.

Invoke through the box queue. The probe measures compute completion, not DWM presents.
Both children are owned here and are terminated on timeout, interruption, or failure.
"""
import argparse
import csv
import json
import math
from pathlib import Path
import subprocess
import time


def distribution(values):
    values = sorted(values)
    if not values:
        raise ValueError("probe produced no samples in the requested overlap")
    def percentile(p):
        return values[min(len(values) - 1, math.ceil(p * len(values)) - 1)]
    return {"samples": len(values), "mean": sum(values) / len(values),
            "p50": percentile(.5), "p95": percentile(.95),
            "p99": percentile(.99), "max": values[-1]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--spirv", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--seconds", type=float, default=20)
    parser.add_argument("--period-ns", type=int, default=16_666_666)
    parser.add_argument("--timeout", type=float, default=240)
    parser.add_argument("--command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.seconds <= 0 or args.period_ns <= 0 or args.timeout <= args.seconds:
        parser.error("duration/period must be positive, timeout must exceed probe duration")
    args.output.mkdir(parents=True, exist_ok=True)
    children = []
    started = time.monotonic()
    heavy_finished_ms = None
    render_started_ns = None
    heavy_finished_ns = None
    try:
        with (args.output / "probe.log").open("w") as probe_log, \
             (args.output / "vixen.log").open("w") as heavy_log:
            probe = subprocess.Popen([str(args.probe.resolve()), str(args.spirv.resolve()),
                                      str((args.output / "probe.csv").resolve()),
                                      str(args.seconds), str(args.period_ns)],
                                     stdout=probe_log, stderr=subprocess.STDOUT)
            children.append(probe)
            heavy = None
            if args.command:
                heavy = subprocess.Popen(args.command, stdout=heavy_log, stderr=subprocess.STDOUT)
                children.append(heavy)
            log_cursor = 0
            while any(child.poll() is None for child in children):
                elapsed = time.monotonic() - started
                if elapsed > args.timeout:
                    raise TimeoutError("probe/VIXEN exceeded the bounded witness timeout")
                if heavy and heavy.poll() is not None and heavy_finished_ms is None:
                    heavy_finished_ms = elapsed * 1000
                    heavy_finished_ns = time.monotonic_ns()
                if heavy and render_started_ns is None:
                    with (args.output / "vixen.log").open() as live_log:
                        live_log.seek(log_cursor)
                        new_output = live_log.read()
                        # Retain a short overlap in case the marker straddles two reads.
                        log_cursor = max(0, live_log.tell() - 80)
                    if "[Run] Entering render loop" in new_output:
                        render_started_ns = time.monotonic_ns()
                time.sleep(.05)
            if heavy and heavy_finished_ms is None:
                heavy_finished_ms = (time.monotonic() - started) * 1000
                heavy_finished_ns = time.monotonic_ns()
            for child in children:
                if child.returncode != 0:
                    raise RuntimeError(f"witness child exited {child.returncode}; see logs")
        with (args.output / "probe.csv").open() as source:
            samples = [{key: float(value) for key, value in row.items()}
                       for row in csv.DictReader(source)]
        if args.command and render_started_ns is None:
            raise ValueError("VIXEN render-loop marker was not observed; no heavy-run cadence claim")
        # Linux steady_clock and Python monotonic share CLOCK_MONOTONIC. Select the observed
        # render interval (50ms polling resolution), excluding startup and the first overlap frame.
        selected = [row for row in samples if row["frame"] > 1 and
                    (render_started_ns is None or row["complete_monotonic_ns"] > render_started_ns) and
                    (heavy_finished_ns is None or row["complete_monotonic_ns"] < heavy_finished_ns)]
        if args.command:
            selected = selected[1:]
        result = {"measurement": "second Vulkan compute client completion (no present)",
                  "period_ns": args.period_ns, "command": args.command,
                  "wall_seconds": time.monotonic() - started,
                  "heavy_process_lifetime_ms": heavy_finished_ms,
                  "render_started_monotonic_ns": render_started_ns,
                  "heavy_finished_monotonic_ns": heavy_finished_ns,
                  "render_marker_poll_interval_ms": 50,
                  "service_ms": distribution([row["service_ms"] for row in selected]),
                  "cadence_ms": distribution([row["cadence_ms"] for row in selected]),
                  "deadline_lateness_ms": distribution([row["deadline_lateness_ms"] for row in selected]),
                  "frames_over_two_periods": sum(row["cadence_ms"] > args.period_ns / 500_000
                                                for row in selected)}
        (args.output / "summary.json").write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps(result, indent=2))
    finally:
        for child in children:
            if child.poll() is None:
                child.terminate()
        for child in children:
            try:
                child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()


if __name__ == "__main__":
    main()
