#!/usr/bin/env python3
"""Summarize native A/B/A look-dev capture CSVs without third-party packages."""

import argparse
import csv
import json
import statistics
from pathlib import Path


CONFIGS = (
    ("linear-scale1", 1.0, "linear"),
    ("linear-scale-2-3", 2.0 / 3.0, "linear"),
    ("linear-scale-1-2", 0.5, "linear"),
    ("fsr1-scale1", 1.0, "fsr1"),
    ("fsr1-scale-2-3", 2.0 / 3.0, "fsr1"),
    ("fsr1-scale-1-2", 0.5, "fsr1"),
)
PRIMARY_COLUMNS = ("esvo_traverse_shade_ms", "shadow_visibility_wave_ms", "spatial_reuse_ms")
FSR_COLUMNS = ("fsr_input_aa_ms", "fsr1_easu_ms", "fsr1_rcas_ms")


def percentile(values, percentile_value):
    ordered = sorted(values)
    if not ordered:
        raise ValueError("cannot compute a percentile from no samples")
    index = (len(ordered) - 1) * percentile_value / 100.0
    lower = int(index)
    upper = min(lower + 1, len(ordered) - 1)
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (index - lower)


def read_run(path):
    with path.open(newline="", encoding="utf-8-sig") as stream:
        rows = list(csv.DictReader(stream))
    if not rows:
        raise ValueError(f"empty performance CSV: {path}")
    headers = rows[0].keys()
    required = {*PRIMARY_COLUMNS, "whole_frame_gpu_span_ms", "frame"}
    missing = required.difference(headers)
    if missing:
        raise ValueError(f"{path}: missing CSV columns: {', '.join(sorted(missing))}")

    selected = []
    for row in rows:
        frame = int(row["frame"])
        within_yaw = frame % 64
        # Retain frames 33-64 from each fixed 64-frame yaw block.
        if 1 <= within_yaw <= 32:
            continue
        selected.append(row)
    if len(selected) != 128:
        raise ValueError(f"{path}: expected 128 steady rows, found {len(selected)}")

    primary = [sum(float(row[column]) for column in PRIMARY_COLUMNS) for row in selected]
    displayed = [float(row["whole_frame_gpu_span_ms"]) for row in selected]
    result = {
        "primary_p50": percentile(primary, 50),
        "primary_p95": percentile(primary, 95),
        "displayed_p50": percentile(displayed, 50),
        "displayed_p95": percentile(displayed, 95),
        "steady_samples": len(selected),
    }
    for column in FSR_COLUMNS:
        if column in headers:
            result[column.removesuffix("_ms") + "_p50"] = percentile(
                [float(row[column]) for row in selected], 50
            )
    return result


def summary(values):
    return {"median": statistics.median(values), "min": min(values), "max": max(values)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path, help="paired-native run output root")
    parser.add_argument("--rounds", type=int, default=3)
    args = parser.parse_args()

    trial_rows = []
    aggregate = []
    for width, height in ((500, 500), (1920, 1080)):
        extent = f"{width}x{height}"
        for config_name, scale, upscaler in CONFIGS:
            trial_metrics = []
            for round_number in range(1, args.rounds + 1):
                base = args.root / extent / f"round-{round_number}" / config_name
                before = read_run(base / "A-before" / "perf.csv")
                candidate = read_run(base / "B-candidate" / "perf.csv")
                after = read_run(base / "A-after" / "perf.csv")

                trial = {
                    "extent": extent,
                    "round": round_number,
                    "config": config_name,
                    "scale": scale,
                    "upscaler": upscaler,
                }
                for metric in ("primary_p50", "primary_p95", "displayed_p50", "displayed_p95"):
                    control = (before[metric] + after[metric]) / 2.0
                    trial[f"native_{metric}"] = control
                    trial[f"candidate_{metric}"] = candidate[metric]
                    if metric.endswith("p50"):
                        trial[f"{metric.removesuffix('_p50')}_gain_pct"] = 100.0 * (control - candidate[metric]) / control
                    else:
                        trial[f"{metric.removesuffix('_p95')}_p95_delta_pct"] = 100.0 * (candidate[metric] - control) / control
                for key, value in candidate.items():
                    if key.endswith("_p50") and key.startswith("fsr"):
                        trial[key] = value
                trial_metrics.append(trial)
                trial_rows.append(trial)

            def gains(prefix):
                return [trial[f"{prefix}_gain_pct"] for trial in trial_metrics]

            def regressions(prefix):
                return [trial[f"{prefix}_p95_delta_pct"] for trial in trial_metrics]

            combined = {
                "extent": extent,
                "config": config_name,
                "scale": scale,
                "upscaler": upscaler,
                "rounds": args.rounds,
                "primary_gain_pct": summary(gains("primary")),
                "displayed_gain_pct": summary(gains("displayed")),
                "primary_p95_delta_pct": summary(regressions("primary")),
                "displayed_p95_delta_pct": summary(regressions("displayed")),
            }
            for metric in ("primary_p50", "primary_p95", "displayed_p50", "displayed_p95"):
                combined[f"native_{metric}"] = summary([trial[f"native_{metric}"] for trial in trial_metrics])
                combined[f"candidate_{metric}"] = summary([trial[f"candidate_{metric}"] for trial in trial_metrics])
            for key in FSR_COLUMNS:
                summary_key = key.removesuffix("_ms") + "_p50"
                values = [trial[summary_key] for trial in trial_metrics if summary_key in trial]
                if values:
                    combined[summary_key] = summary(values)

            thresholds = {2.0 / 3.0: 30.0, 0.5: 50.0}
            if scale in thresholds:
                primary_pass = combined["primary_gain_pct"]["median"] >= thresholds[scale]
                displayed_pass = combined["displayed_gain_pct"]["median"] >= 20.0
                no_tail_regression = combined["displayed_p95_delta_pct"]["max"] <= 0.0
                combined["gate"] = {
                    "primary_pass": primary_pass,
                    "displayed_pass": displayed_pass,
                    "no_displayed_p95_regression": no_tail_regression,
                    "pass": primary_pass and displayed_pass and no_tail_regression,
                }
            else:
                combined["gate"] = None
            aggregate.append(combined)

    with (args.root / "paired-trials.csv").open("w", newline="", encoding="utf-8") as stream:
        fields = sorted({key for row in trial_rows for key in row})
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(trial_rows)
    (args.root / "paired-summary.json").write_text(json.dumps(aggregate, indent=2) + "\n", encoding="utf-8")

    for row in aggregate:
        p = row["primary_gain_pct"]
        d = row["displayed_gain_pct"]
        t = row["displayed_p95_delta_pct"]
        gate = row["gate"]
        gate_text = "n/a" if gate is None else ("PASS" if gate["pass"] else "FAIL")
        print(
            f"{row['extent']} {row['config']}: "
            f"primary gain {p['median']:.2f}% [{p['min']:.2f}, {p['max']:.2f}], "
            f"displayed gain {d['median']:.2f}% [{d['min']:.2f}, {d['max']:.2f}], "
            f"displayed p95 delta {t['median']:+.2f}% [{t['min']:+.2f}, {t['max']:+.2f}], "
            f"gate {gate_text}"
        )


if __name__ == "__main__":
    main()
