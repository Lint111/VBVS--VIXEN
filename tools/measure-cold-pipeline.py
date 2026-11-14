#!/usr/bin/env python3
"""Run a command under a memory cap and report its cold working set per pipeline phase.

On Linux the payload runs under `systemd-run --user --scope -p MemoryMax=<cap>`; on Windows it runs directly and the
sampled process-tree working set is the only memory figure (it is the enforced bound on both). VIXEN_SHADER_DUMP_DIR
pointed at <out>/dump. ShaderCompiler writes one `<stem>-<stage>-<hash>.spv` there per compile, so
each file's arrival time closes a phase: the phase named after a file spans from the previous
arrival (or launch) to that file, and its peak is the largest cgroup memory.current sampled in
the span. Time after the last dump is reported as `post-compile`. Writes <out>/samples.tsv,
<out>/phases.tsv and <out>/summary.json, and exits non-zero when the cap killed the payload, the
payload failed, the sampled process-tree RSS passed --cap (the enforced bound: inside a queue job
the payload may leave the scope's cgroup, so memory.current/peak can read near zero while
tree_rss_peak_bytes is the real working set), or --expect-file is missing or empty.

Usage: tools/measure-cold-pipeline.py --out DIR [--cap 16G] [--interval 0.5] [--timeout S]
           [--expect-file PATH] [--fresh DIR]... [--payload-exe EXE] -- command...
Run it inside one queue job (it is not queue-aware); never --wait with --scope.
"""
import argparse
import ctypes
import json
import os
import re
import shutil
import subprocess
import sys
import time

UNITS = {"K": 1 << 10, "M": 1 << 20, "G": 1 << 30}


def parse_bytes(text):
    m = re.fullmatch(r"(\d+)([KMG]?)", text)
    if not m:
        raise argparse.ArgumentTypeError(f"bad size {text!r} (use e.g. 16G)")
    return int(m.group(1)) * UNITS.get(m.group(2), 1)


def cgroup_dir(pid):
    with open(f"/proc/{pid}/cgroup") as handle:
        for line in handle:
            if line.startswith("0::"):
                path = line[3:].strip()
                return "/sys/fs/cgroup" + path if os.path.basename(path).startswith("run-") else None
    return None


def read_int(path):
    try:
        with open(path) as handle:
            return int(handle.read().strip())
    except (OSError, ValueError):
        return None


def read_events(path):
    events = {}
    try:
        with open(path) as handle:
            for line in handle:
                key, value = line.split()
                events[key] = int(value)
    except (OSError, ValueError):
        pass
    return events


def windows_process_table():
    """{pid: (parent pid, working set bytes)} for every process this user can open (Windows)."""
    from ctypes import wintypes

    class ProcessEntry(ctypes.Structure):
        _fields_ = [("dwSize", wintypes.DWORD), ("cntUsage", wintypes.DWORD), ("th32ProcessID", wintypes.DWORD),
                    ("th32DefaultHeapID", ctypes.c_size_t), ("th32ModuleID", wintypes.DWORD),
                    ("cntThreads", wintypes.DWORD), ("th32ParentProcessID", wintypes.DWORD),
                    ("pcPriClassBase", wintypes.LONG), ("dwFlags", wintypes.DWORD), ("szExeFile", wintypes.WCHAR * 260)]

    class MemoryCounters(ctypes.Structure):
        _fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD), ("PeakWorkingSetSize", ctypes.c_size_t),
                    ("WorkingSetSize", ctypes.c_size_t), ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
                    ("QuotaPagedPoolUsage", ctypes.c_size_t), ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
                    ("QuotaNonPagedPoolUsage", ctypes.c_size_t), ("PagefileUsage", ctypes.c_size_t),
                    ("PeakPagefileUsage", ctypes.c_size_t)]

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    psapi = ctypes.WinDLL("psapi", use_last_error=True)
    kernel32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    kernel32.OpenProcess.restype = wintypes.HANDLE
    kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
    psapi.GetProcessMemoryInfo.argtypes = [wintypes.HANDLE, ctypes.POINTER(MemoryCounters), wintypes.DWORD]
    table = {}
    snapshot = kernel32.CreateToolhelp32Snapshot(0x2, 0)  # TH32CS_SNAPPROCESS
    if snapshot in (None, wintypes.HANDLE(-1).value):
        return table
    try:
        entry = ProcessEntry()
        entry.dwSize = ctypes.sizeof(entry)
        more = kernel32.Process32FirstW(wintypes.HANDLE(snapshot), ctypes.byref(entry))
        while more:
            working_set = 0
            handle = kernel32.OpenProcess(0x1010, False, entry.th32ProcessID)  # QUERY_LIMITED_INFORMATION | VM_READ
            if handle:
                counters = MemoryCounters()
                counters.cb = ctypes.sizeof(counters)
                if psapi.GetProcessMemoryInfo(handle, ctypes.byref(counters), counters.cb):
                    working_set = counters.WorkingSetSize
                kernel32.CloseHandle(handle)
            table[entry.th32ProcessID] = (entry.th32ParentProcessID, working_set)
            more = kernel32.Process32NextW(wintypes.HANDLE(snapshot), ctypes.byref(entry))
    finally:
        kernel32.CloseHandle(wintypes.HANDLE(snapshot))
    return table


def linux_process_table():
    """{pid: (parent pid, VmRSS bytes)} from /proc."""
    table = {}
    for entry in os.listdir("/proc"):
        if not entry.isdigit():
            continue
        try:
            with open(f"/proc/{entry}/status") as handle:
                fields = dict(line.split(":", 1) for line in handle if ":" in line)
            table[int(entry)] = (int(fields["PPid"].strip()), int(fields.get("VmRSS", "0 kB").split()[0]) * 1024)
        except (OSError, ValueError, KeyError):
            continue
    return table


def tree_rss(root_pid):
    """Sum resident bytes over root_pid and its descendants (None when the root is gone)."""
    table = windows_process_table() if os.name == "nt" else linux_process_table()
    parents = {pid: parent for pid, (parent, _) in table.items()}
    rss = {pid: resident for pid, (_, resident) in table.items()}
    if root_pid not in parents:
        return None
    members, grew = {root_pid}, True
    while grew:
        grew = False
        for pid, ppid in parents.items():
            if ppid in members and pid not in members:
                members.add(pid)
                grew = True
    return sum(rss[pid] for pid in members)


def dump_arrivals(dump_dir):
    arrivals = []
    if os.path.isdir(dump_dir):
        for name in os.listdir(dump_dir):
            if name.endswith(".spv"):
                arrivals.append((os.path.getmtime(os.path.join(dump_dir, name)), name[:-4]))
    return sorted(arrivals)


def build_phases(samples, arrivals, start, end):
    """samples: [(t, bytes)]; returns [(name, t0, t1, peak_bytes)] covering start..end."""
    marks = [(t, name) for t, name in arrivals if start <= t <= end]
    phases, t0 = [], start
    for t1, name in marks + [(end, "post-compile")]:
        window = [b for t, b in samples if t0 <= t <= t1]
        phases.append((name, t0 - start, t1 - start, max(window) if window else 0))
        t0 = t1
    return phases


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    parser.add_argument("--out", required=True)
    parser.add_argument("--cap", type=parse_bytes, default=parse_bytes("16G"))
    parser.add_argument("--interval", type=float, default=0.5)
    parser.add_argument("--timeout", type=float, default=1500.0)
    parser.add_argument("--expect-file")
    parser.add_argument("--fresh", action="append", default=[], help="directory removed before launch (cold caches)")
    parser.add_argument("--payload-exe", help="executable the command ends up running; must exist (lets CTest classify the wrapper as the payload's class)")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command:
        parser.error("no command after --")

    if args.payload_exe and not os.path.isfile(args.payload_exe):
        print(f"payload executable missing: {args.payload_exe}", file=sys.stderr)
        return 2
    for stale in args.fresh:
        shutil.rmtree(stale, ignore_errors=True)
    dump_dir = os.path.join(args.out, "dump")
    os.makedirs(dump_dir, exist_ok=True)
    env = dict(os.environ, VIXEN_SHADER_DUMP_DIR=dump_dir)
    start = time.time()
    scope = [] if os.name == "nt" else ["systemd-run", "--user", "--scope", "--quiet", "-p", f"MemoryMax={args.cap}",
                                        "-p", "MemorySwapMax=0", "--"]
    proc = subprocess.Popen(scope + command, env=env, stdin=subprocess.DEVNULL)

    samples, rss_samples, peak_file, events, cgroup, timed_out = [], [], None, {}, None, False
    rss_cap_exceeded = False
    while proc.poll() is None:
        now = time.time()
        if now - start > args.timeout:
            timed_out = True
            proc.terminate()
            break
        if cgroup is None and os.name != "nt":
            cgroup = cgroup_dir(proc.pid)
        if cgroup and os.path.isdir(cgroup):
            current = read_int(os.path.join(cgroup, "memory.current"))
            if current is not None:
                samples.append((now, current))
            peak_file = read_int(os.path.join(cgroup, "memory.peak")) or peak_file
            events = read_events(os.path.join(cgroup, "memory.events")) or events
        rss = tree_rss(proc.pid)
        if rss is not None:
            rss_samples.append((now, rss))
            if rss > args.cap:
                # Under a queue job the payload can leave the scope's cgroup, so MemoryMax alone
                # does not bound it; the sampled process-tree RSS is the enforced limit.
                rss_cap_exceeded = True
                proc.terminate()
                break
        time.sleep(args.interval)
    try:
        proc.wait(timeout=60)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
    end = time.time()

    phases = build_phases(sorted(samples + rss_samples), dump_arrivals(dump_dir), start, end)
    with open(os.path.join(args.out, "samples.tsv"), "w") as handle:
        handle.write("elapsed_s\tcgroup_memory_current_bytes\ttree_rss_bytes\n")
        rss_by_time = dict(rss_samples)
        handle.writelines(f"{t - start:.2f}\t{b}\t{rss_by_time.get(t, '')}\n" for t, b in samples)
    with open(os.path.join(args.out, "phases.tsv"), "w") as handle:
        handle.write("phase\tstart_s\tend_s\tpeak_bytes\n")
        handle.writelines(f"{n}\t{a:.2f}\t{b:.2f}\t{p}\n" for n, a, b, p in phases)

    frame_bytes = os.path.getsize(args.expect_file) if args.expect_file and os.path.exists(args.expect_file) else None
    summary = {
        "command": command, "cap_bytes": args.cap, "exit_code": proc.returncode, "timed_out": timed_out, "rss_cap_exceeded": rss_cap_exceeded,
        "wall_s": round(end - start, 2), "sampled_peak_bytes": max((b for _, b in samples), default=0),
        "tree_rss_peak_bytes": max((b for _, b in rss_samples), default=0), "cgroup": cgroup,
        "cgroup_memory_peak_bytes": peak_file, "oom_kill": events.get("oom_kill", 0),
        "memory_max_hits": events.get("max", 0), "shader_dumps": len(dump_arrivals(dump_dir)),
        "expect_file": args.expect_file, "expect_file_bytes": frame_bytes,
        "phases": [{"phase": n, "start_s": round(a, 2), "end_s": round(b, 2), "peak_bytes": p}
                   for n, a, b, p in phases],
    }
    with open(os.path.join(args.out, "summary.json"), "w") as handle:
        json.dump(summary, handle, indent=2)
    print(json.dumps({k: v for k, v in summary.items() if k != "phases"}))
    for phase in sorted(summary["phases"], key=lambda p: -p["peak_bytes"])[:8]:
        print(f"  peak {phase['peak_bytes'] / (1 << 20):9.1f} MiB  {phase['start_s']:7.1f}-{phase['end_s']:7.1f}s  {phase['phase']}")

    failed = proc.returncode != 0 or timed_out or rss_cap_exceeded or summary["oom_kill"] > 0
    if args.expect_file and not frame_bytes:
        failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
