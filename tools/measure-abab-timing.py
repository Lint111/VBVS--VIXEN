#!/usr/bin/env python3
"""Warmed, interleaved A/B/A timing comparison of the look-dev capture runner (T-2903).

Runs the same capture command once per variant as discarded warm-up, then rounds in which the
variant order alternates (A B | B A | A B ...) so run order and slow device drift cannot favour one
variant. Each run records the `[CAPTURE_TIMING] frame_p50_ms=` line(s) and a device-state sample
(GPU clock, power, temperature) taken before and after the run. A ranking is printed only when
every variant is stable across rounds and the gap between variants exceeds that spread; otherwise
the verdict is INCONCLUSIVE and says why.

Invoke each comparison through the box queue as one benchmark job (--resource gpu --gpu-benchmark)
on the native Windows build; Dozen timings are correctness-only.

  tools/measure-abab-timing.py --output out --rounds 3 --variant fsr1=VIXEN_UPSCALER=fsr1 \\
      --variant linear=VIXEN_UPSCALER=linear -- vixen_lookdev_capture.exe --gtest_filter=...
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import sys

P50 = re.compile(r"\[CAPTURE_TIMING\].*?\bframe_p50_ms=([0-9.]+)")
P95 = re.compile(r"\[CAPTURE_TIMING\].*?\bframe_p95_ms=([0-9.]+)")
NVIDIA_QUERY = "clocks.sm,clocks.mem,power.draw,temperature.gpu"
NVIDIA_KEYS = ["sm_clock_mhz", "mem_clock_mhz", "power_w", "temp_c"]


def parse_variant(text):
    name, sep, assignments = text.partition("=")
    if not name or not sep:
        raise argparse.ArgumentTypeError(
            f"--variant '{text}' must look like NAME=ENV=VALUE[,ENV=VALUE...] (e.g. fsr1=VIXEN_UPSCALER=fsr1)")
    env = {}
    for item in filter(None, assignments.split(",")):
        key, eq, value = item.partition("=")
        if not key or not eq:
            raise argparse.ArgumentTypeError(f"--variant {name}: '{item}' is not ENV=VALUE")
        env[key] = value
    return name, env


def schedule(names, rounds):
    """Alternate the direction each round so every variant sees both early and late slots."""
    order = []
    for index in range(rounds):
        order.append(list(names) if index % 2 == 0 else list(reversed(names)))
    return order


def parse_timing(stdout):
    p50 = [float(v) for v in P50.findall(stdout)]
    p95 = [float(v) for v in P95.findall(stdout)]
    if not p50:
        raise ValueError("no '[CAPTURE_TIMING] ... frame_p50_ms=' line in the capture output; "
                         "the command must run the look-dev capture runner")
    return {"p50_ms": statistics.median(p50), "p95_ms": max(p95) if p95 else None, "timing_lines": len(p50)}


def sample_device(telemetry_cmd=None):
    """One device-state sample; always returns a dict, with 'unavailable' naming the reason."""
    if telemetry_cmd:
        done = subprocess.run(telemetry_cmd, shell=True, capture_output=True, text=True, stdin=subprocess.DEVNULL)
        if done.returncode != 0:
            return {"unavailable": f"--telemetry-cmd exited {done.returncode}: {done.stderr.strip()[:200]}"}
        try:
            return json.loads(done.stdout)
        except ValueError:
            return {"unavailable": "--telemetry-cmd must print one JSON object of numbers"}
    smi = shutil.which("nvidia-smi")
    if smi:
        done = subprocess.run([smi, f"--query-gpu={NVIDIA_QUERY}", "--format=csv,noheader,nounits"],
                              capture_output=True, text=True, stdin=subprocess.DEVNULL)
        cells = [c.strip() for c in done.stdout.splitlines()[0].split(",")] if done.stdout.strip() else []
        if done.returncode == 0 and len(cells) == len(NVIDIA_KEYS):
            return {k: float(c) for k, c in zip(NVIDIA_KEYS, cells) if c.replace(".", "", 1).isdigit()}
    sample = {}
    for card in sorted(Path("/sys/class/drm").glob("card[0-9]/device")):
        sclk = card / "pp_dpm_sclk"
        if sclk.is_file():
            active = [l for l in sclk.read_text().splitlines() if l.rstrip().endswith("*")]
            match = re.search(r"([0-9]+)\s*Mhz", active[0], re.I) if active else None
            if match:
                sample["sclk_mhz"] = float(match.group(1))
        for power in card.glob("hwmon/hwmon*/power1_average"):
            sample["power_w"] = int(power.read_text()) / 1e6
        if sample:
            return sample
    return {"unavailable": "no nvidia-smi and no amdgpu sysfs clock; pass --telemetry-cmd "
                           "'<cmd printing one JSON object>' (e.g. a PowerShell GPU counter query on Windows)"}


def run_once(command, env_overrides, telemetry_cmd):
    before = sample_device(telemetry_cmd)
    done = subprocess.run(command, env={**os.environ, **env_overrides}, capture_output=True, text=True,
                          stdin=subprocess.DEVNULL)
    after = sample_device(telemetry_cmd)
    if done.returncode != 0:
        raise RuntimeError(f"capture command exited {done.returncode}: {' '.join(command)}\n"
                           f"{(done.stdout + done.stderr)[-1500:]}")
    return {**parse_timing(done.stdout), "device_before": before, "device_after": after}


def spread_pct(values):
    mid = statistics.median(values)
    return 100.0 * (max(values) - min(values)) / mid if mid else 0.0


def judge(variant_p50, stability_pct):
    """Per-variant median/spread, and a ranking only when it survives the measured spread."""
    summary = {n: {"median_p50_ms": statistics.median(v), "spread_pct": spread_pct(v), "runs": len(v)}
               for n, v in variant_p50.items()}
    unstable = [n for n, s in summary.items() if s["runs"] < 2 or s["spread_pct"] > stability_pct]
    ranked = sorted(summary, key=lambda n: summary[n]["median_p50_ms"])
    if unstable:
        return summary, "INCONCLUSIVE", (
            f"unstable variants {unstable}: p50 spread across rounds exceeds {stability_pct}% (or fewer than "
            "2 rounds); check device_before/device_after clocks, raise --warmup/--rounds, rerun")
    gap = 100.0 * (summary[ranked[-1]]["median_p50_ms"] - summary[ranked[0]]["median_p50_ms"]) \
        / summary[ranked[0]]["median_p50_ms"]
    noise = max(s["spread_pct"] for s in summary.values())
    if gap <= noise:
        return summary, "INCONCLUSIVE", f"variant gap {gap:.1f}% is within the run-to-run spread {noise:.1f}%"
    return summary, "RANKED", f"{' < '.join(ranked)} by median p50 (gap {gap:.1f}% > spread {noise:.1f}%)"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--variant", action="append", type=parse_variant, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--rounds", type=int, default=3)
    parser.add_argument("--warmup", type=int, default=1, help="discarded runs per variant before measuring")
    parser.add_argument("--stability-pct", type=float, default=10.0)
    parser.add_argument("--telemetry-cmd", help="shell command printing one JSON object of device numbers")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if len(args.variant) < 2 or not command or args.rounds < 2 or args.warmup < 0:
        parser.error("need >= 2 --variant, >= 2 --rounds and a capture command after '--'")
    variants = dict(args.variant)
    args.output.mkdir(parents=True, exist_ok=True)
    runs = []
    for index in range(args.warmup):
        for name in variants:
            print(f"[abab] warm-up {index + 1}/{args.warmup} {name}", flush=True)
            run_once(command, variants[name], args.telemetry_cmd)
    for index, order in enumerate(schedule(variants, args.rounds)):
        for name in order:
            result = run_once(command, variants[name], args.telemetry_cmd)
            runs.append({"round": index, "variant": name, **result})
            print(f"[abab] round {index} {name}: p50={result['p50_ms']:.2f} ms device={result['device_after']}",
                  flush=True)
    by_variant = {n: [r["p50_ms"] for r in runs if r["variant"] == n] for n in variants}
    summary, verdict, reason = judge(by_variant, args.stability_pct)
    telemetry = not any("unavailable" in r["device_before"] or "unavailable" in r["device_after"] for r in runs)
    report = {"command": command, "variants": variants, "warmup": args.warmup, "rounds": args.rounds,
              "device_telemetry": telemetry, "summary": summary, "verdict": verdict, "reason": reason, "runs": runs}
    (args.output / "abab.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"[abab] verdict {verdict}: {reason}")
    if not telemetry:
        print("[abab] WARNING: no device-state telemetry for some runs; clock/power drift cannot be ruled out",
              file=sys.stderr)
    return 0 if verdict == "RANKED" else 3


if __name__ == "__main__":
    sys.exit(main())
