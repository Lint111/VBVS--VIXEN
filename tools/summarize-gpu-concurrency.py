#!/usr/bin/env python3
"""Summarise tools/measure-gpu-concurrency.sh runs: wall, CPU, GPU-test time, occupancy, identity.

Usage: summarize-gpu-concurrency.py RUN_PARENT_DIR... [--done-log FILE] [--reference RUN_DIR]
Each RUN_PARENT_DIR holds run directories (n<slots>-r<repeat>) written by the measure script.
Per run it prints: wall seconds, queue-job CPU seconds (from the box-queue done log, by label suffix),
sum of `gpu`-label test time and its share of wall (GPU occupancy, tests running per second),
mean/max of the host GPU-engine sampler when present, failed tests, and whether the set of failed
tests and the sorted PNG sha256 list equal the reference run's.
"""
import argparse, glob, os, re, sys
import xml.etree.ElementTree as ET


def read(path):
    try:
        return open(path, encoding="utf-8", errors="replace").read()
    except OSError:
        return ""


def junit(path):
    root = ET.parse(path).getroot()
    out = {}
    for case in root.iter("testcase"):
        status = case.get("status", "")
        failed = case.find("failure") is not None or case.find("error") is not None or status in ("fail", "failed")
        skipped = case.find("skipped") is not None or status in ("notrun", "skipped")
        out[case.get("name")] = (float(case.get("time", "0")), "fail" if failed else "skip" if skipped else "pass")
    return out


def run_info(d, done_log, sub_label):
    s = read(os.path.join(d, "summary.txt"))
    wall = int(re.search(r"wall_s=(\d+)", s).group(1)) if "wall_s=" in s else -1
    stdout = read(os.path.join(d, "ctest-stdout.log"))
    m = re.search(r"^gpu\s+=\s+([\d.]+) sec\*proc \((\d+) tests?\)", stdout, re.M)
    gpu_time, gpu_tests = (float(m.group(1)), int(m.group(2))) if m else (float("nan"), 0)
    tests = junit(os.path.join(d, "junit.xml")) if os.path.exists(os.path.join(d, "junit.xml")) else {}
    cpu = float("nan")
    label = f"{sub_label}-{os.path.basename(d)}"
    for line in reversed(read(done_log).splitlines()):
        if f" {label} " in line and " exit=" in line:
            c = re.search(r"cpu=([\d.]+)s", line)
            cpu = float(c.group(1)) if c else cpu
            break
    samp = []
    for line in read(os.path.join(d, "gpu-sampler.csv")).splitlines()[1:]:
        p = line.split(",")
        if len(p) >= 3 and p[1] != "err":
            try:
                samp.append(float(p[1]))
            except ValueError:
                pass
    pngs = read(os.path.join(d, "png-sha256.txt"))
    return dict(dir=d, wall=wall, cpu=cpu, gpu_time=gpu_time, gpu_tests=gpu_tests, tests=tests, samp=samp, pngs=pngs)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("parents", nargs="+")
    ap.add_argument("--done-log", default=os.path.expanduser("~/.local/state/undertow/undertow-box-done.log"))
    ap.add_argument("--reference")
    a = ap.parse_args()
    runs = []
    for parent in a.parents:
        sub = os.path.basename(parent.rstrip("/"))
        for d in sorted(glob.glob(os.path.join(parent, "n*-r*"))):
            if os.path.isdir(d):
                runs.append(run_info(d, a.done_log, f"sn-rgconc1:{sub}"))
    ref = run_info(a.reference, a.done_log, "") if a.reference else runs[0]
    ref_fail = sorted(n for n, (_, st) in ref["tests"].items() if st != "pass")
    print(f"reference {ref['dir']}: failed/skipped={len(ref_fail)} pngs={len(ref['pngs'].splitlines())}")
    print("run                        wall_s  cpu_s  gpu_test_s  occupancy  gpu_sampler_mean/max  failed  same_fail_set  same_pngs")
    for r in runs:
        fail = sorted(n for n, (_, st) in r["tests"].items() if st != "pass")
        occ = r["gpu_time"] / r["wall"] if r["wall"] > 0 else float("nan")
        mean = sum(r["samp"]) / len(r["samp"]) if r["samp"] else float("nan")
        mx = max(r["samp"]) if r["samp"] else float("nan")
        print(f"{os.path.basename(os.path.dirname(r['dir']))}/{os.path.basename(r['dir']):<14} {r['wall']:>6} {r['cpu']:>6.0f} {r['gpu_time']:>10.1f} {occ:>9.2f} "
              f"{mean:>10.2f}/{mx:<8.2f} {len(fail):>6} {str(fail == ref_fail):>13} {str(r['pngs'] == ref['pngs']):>9}")


main()
