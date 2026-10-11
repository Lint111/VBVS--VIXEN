#!/usr/bin/env python3
"""Summarize elapsed GPU-clock intervals separately from host scheduling calls.

GPU-clock time may include preemption. It is not an active-execution counter.
No samples, incomplete producers and budget overruns fail the budget verdict.
"""
import argparse
import csv
import json
import math
from pathlib import Path
import re


def distribution(values):
    values = sorted(values)
    if not values:
        return None
    return {"samples": len(values), "mean_ms": sum(values) / len(values),
            "p99_ms": values[math.ceil(.99 * len(values)) - 1], "max_ms": values[-1]}


def summarize(log, probe, expected_frames, node=None, aggregate_budget_ms=None):
    rows = log.read_text(errors="replace").splitlines() if log else []
    chunks, dispatches, frames, budgets, counts, hosts = [], [], [], [], [], {}
    submissions, submission_counts, totals = [], [], []
    invalid_timestamp_pairs = 0
    zero_resolution_dispatch_pairs = 0
    for row in rows:
        if node and any(tag in row for tag in ("[GPUYIELD]", "[GPUYIELD_CHUNK]", "[GPUYIELD_SUBMISSION]")):
            fields = dict(re.findall(r"(\w+)=([^ ]+)", row))
            if fields.get("node") != node:
                continue
        if "[GPUYIELD_CHUNK]" in row:
            fields = dict(re.findall(r"(\w+)=([^ ]+)", row))
            for prefix in ("", "dispatch_"):
                start = int(fields.get(prefix + "start_ticks", "0"))
                end = int(fields.get(prefix + "end_ticks", "0"))
                # Available inner queries may occupy the same hardware tick.
                # Keep a positive enclosing command interval as the budget witness.
                invalid_timestamp_pairs += start == 0 or (end < start if prefix else end <= start)
                if prefix and start != 0 and end == start:
                    zero_resolution_dispatch_pairs += 1
            chunks.append(float(re.search(r"elapsed_gpu_ms=([\d.]+)", row)[1]))
            if "dispatch_gpu_ms=" in row:
                dispatches.append(float(re.search(r"dispatch_gpu_ms=([\d.]+)", row)[1]))
        if "[GPUYIELD]" in row:
            frames.append(float(re.search(r"max_submission_ms=([\d.]+)", row)[1]))
            budgets.append(float(re.search(r"budget_ms=([\d.]+)", row)[1]))
            fields = dict(re.findall(r"(\w+)=([^ ]+)", row))
            counts.append(int(fields.get("chunks", fields["submissions"])))
            submission_counts.append(int(fields["submissions"]))
            if "total_gpu_ms" in fields:
                totals.append(float(fields["total_gpu_ms"]))
        if "[GPUYIELD_SUBMISSION]" in row:
            submissions.append(float(re.search(r"elapsed_gpu_ms=([\d.]+)", row)[1]))
        if "[GPUYIELD_HOST]" in row:
            operation = re.search(r"operation=([\w-]+)", row)[1]
            elapsed = re.search(r"elapsed_ms=([\d.]+)", row)
            if elapsed:
                ms = float(elapsed[1])
            else:
                start = int(re.search(r"start_ns=(\d+)", row)[1])
                end = int(re.search(r"end_ns=(\d+)", row)[1])
                ms = (end - start) / 1e6
            hosts.setdefault(operation, []).append(ms)
    result = {"gpu_measurement": "elapsed GPU clock, including any preemption",
              "chunks": distribution(chunks), "dispatch_intervals": distribution(dispatches), "frame_maxima": distribution(frames),
              "host_calls": {op: distribution(values) for op, values in hosts.items()},
              "invalid_timestamp_pairs": invalid_timestamp_pairs,
              "zero_resolution_dispatch_pairs": zero_resolution_dispatch_pairs}
    result["submissions"] = distribution(submissions)
    count_distribution = distribution(submission_counts)
    result["submissions_per_frame"] = ({key.removesuffix("_ms"): value
                                        for key, value in count_distribution.items()}
                                       if count_distribution else None)
    result["aggregate_gpu_ms"] = distribution(totals)
    if node:
        result["node"] = node
    if aggregate_budget_ms is not None:
        result["aggregate_budget_ms"] = aggregate_budget_ms
        result["aggregate_budget_pass"] = (len(totals) == expected_frames and
                                             max(totals, default=math.inf) <= aggregate_budget_ms)
    if log:
        result["expected_frames"] = expected_frames
        result["observed_frames"] = len(frames)
        result["budget_ms"] = min(budgets) if budgets else None
        result["budget_pass"] = (invalid_timestamp_pairs == 0 and len(frames) == expected_frames and bool(chunks) and len(chunks) == sum(counts) and
                                  (not submissions or len(submissions) == sum(submission_counts)) and
                                  max(chunks) <= min(budgets) and
                                  all(value <= budget for value, budget in zip(frames, budgets)))
    if probe:
        with probe.open() as source:
            rows = list(csv.DictReader(source))[1:]
        keys = ("gpu_elapsed_ms", "submit_call_ms", "fence_wait_ms", "service_ms", "cadence_ms")
        samples = []
        for row in rows:
            try:
                values = {key: float(row[key]) for key in keys}
            except (KeyError, TypeError, ValueError):
                continue
            if (all(math.isfinite(value) and value >= 0 for value in values.values()) and
                    values["gpu_elapsed_ms"] > 0):
                samples.append(row)
        result["peer_invalid_rows"] = len(rows) - len(samples)
        result["peer_sample_count"] = len(samples)
        result["peer_valid"] = bool(samples) and result["peer_invalid_rows"] == 0
        result["peer"] = {key: distribution([float(row[key]) for row in samples]) for key in keys}
        if samples and samples[0].get("query_read_ms") is not None:
            result["peer"]["query_read_ms"] = distribution([float(row["query_read_ms"]) for row in samples])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", type=Path)
    parser.add_argument("--probe", type=Path)
    parser.add_argument("--frames", type=int, default=500)
    parser.add_argument("--node", help="Restrict GPU samples to one named render pass; host calls remain global")
    parser.add_argument("--aggregate-budget-ms", type=float)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not args.log and not args.probe:
        parser.error("a producer log or peer CSV is required")
    result = summarize(args.log, args.probe, args.frames, args.node, args.aggregate_budget_ms)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    if ((args.log and not result["budget_pass"]) or (args.probe and not result["peer_valid"]) or
            result.get("aggregate_budget_pass") is False):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
