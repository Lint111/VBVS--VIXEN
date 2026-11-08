#!/usr/bin/env python3
"""Derive how many non-benchmark GPU tests may share the Vulkan device (R837.7).

The count comes from the device, never a constant: the largest device-local memory heap (what
the loader reports through vkGetPhysicalDeviceMemoryProperties) and the compute-capable queue
count, divided by the per-test peak. Per-test peaks come from a TSV of "test<TAB>peak_MiB"
(--peaks); a test without a measured peak uses --default-peak-mib. Output is a CTest resource spec
(--resource-spec-file) holding one "gpu" resource whose slots are MiB-sized budget units, plus a
one-line summary on stderr.  Benchmarks keep RESOURCE_LOCK vixen_gpu_device and are not slotted.

Usage: derive-gpu-slots.py [--peaks FILE] [--default-peak-mib 2048] [--headroom 0.8]
                           [--spec-out FILE] [--print-jobs]
"""
import argparse, ctypes, json, sys

U32, U64 = ctypes.c_uint32, ctypes.c_uint64


class MemType(ctypes.Structure):
    _fields_ = [("flags", U32), ("heapIndex", U32)]


class MemHeap(ctypes.Structure):
    _fields_ = [("size", U64), ("flags", U32)]


class MemProps(ctypes.Structure):
    _fields_ = [("typeCount", U32), ("types", MemType * 32), ("heapCount", U32), ("heaps", MemHeap * 16)]


class QueueFamily(ctypes.Structure):
    _fields_ = [("flags", U32), ("count", U32), ("tsBits", U32), ("gran", U32 * 3)]


class AppInfo(ctypes.Structure):
    _fields_ = [("sType", U32), ("pNext", ctypes.c_void_p), ("name", ctypes.c_char_p), ("appVer", U32),
                ("engine", ctypes.c_char_p), ("engVer", U32), ("apiVersion", U32)]


class InstInfo(ctypes.Structure):
    _fields_ = [("sType", U32), ("pNext", ctypes.c_void_p), ("flags", U32), ("app", ctypes.POINTER(AppInfo)),
                ("layers", U32), ("layerNames", ctypes.c_void_p), ("exts", U32), ("extNames", ctypes.c_void_p)]


def probe():
    vk = ctypes.CDLL("libvulkan.so.1")
    app = AppInfo(0, None, b"derive-gpu-slots", 1, b"none", 1, (1 << 22) | (1 << 12))
    info = InstInfo(1, None, 0, ctypes.pointer(app), 0, None, 0, None)
    inst = ctypes.c_void_p()
    if vk.vkCreateInstance(ctypes.byref(info), None, ctypes.byref(inst)) != 0:
        raise SystemExit("derive-gpu-slots: vkCreateInstance failed (is VK_ICD_FILENAMES set? see vixen-gpu-env.sh)")
    n = U32(0)
    vk.vkEnumeratePhysicalDevices(inst, ctypes.byref(n), None)
    if n.value == 0:
        raise SystemExit("derive-gpu-slots: no Vulkan physical device")
    devs = (ctypes.c_void_p * n.value)()
    vk.vkEnumeratePhysicalDevices(inst, ctypes.byref(n), devs)
    props = (ctypes.c_char * 1024)()  # VkPhysicalDeviceProperties; deviceName at byte offset 20
    vk.vkGetPhysicalDeviceProperties(devs[0], props)
    name = bytes(props[20:20 + 256]).split(b"\0")[0].decode(errors="replace")
    mem = MemProps()
    vk.vkGetPhysicalDeviceMemoryProperties(devs[0], ctypes.byref(mem))
    local = max((mem.heaps[i].size for i in range(mem.heapCount) if mem.heaps[i].flags & 1), default=0)
    q = U32(0)
    vk.vkGetPhysicalDeviceQueueFamilyProperties(devs[0], ctypes.byref(q), None)
    fams = (QueueFamily * q.value)()
    vk.vkGetPhysicalDeviceQueueFamilyProperties(devs[0], ctypes.byref(q), fams)
    compute_queues = sum(f.count for f in fams if f.flags & 0x2 or f.flags & 0x1)
    return name, local // (1 << 20), compute_queues


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--peaks"); ap.add_argument("--default-peak-mib", type=int, default=2048)
    ap.add_argument("--headroom", type=float, default=0.8); ap.add_argument("--spec-out")
    ap.add_argument("--print-jobs", action="store_true")
    a = ap.parse_args()
    name, heap_mib, queues = probe()
    peaks = {}
    if a.peaks:
        for line in open(a.peaks, encoding="utf-8"):
            parts = line.rstrip("\n").split("\t")
            if len(parts) == 2 and parts[1].isdigit():
                peaks[parts[0]] = int(parts[1])
    typical = sorted(peaks.values())[len(peaks) // 2] if peaks else a.default_peak_mib
    budget = int(heap_mib * a.headroom)
    by_memory = max(1, budget // max(1, typical))
    jobs = max(1, min(by_memory, max(1, queues)))
    spec = {"version": {"major": 1, "minor": 0},
            "local": [{"gpu": [{"id": "0", "slots": budget}]}]}
    if a.spec_out:
        json.dump(spec, open(a.spec_out, "w"), indent=1)
    print(f"derive-gpu-slots: device='{name}' device-local={heap_mib}MiB queues={queues} budget={budget}MiB "
          f"typical-peak={typical}MiB({'measured' if peaks else 'default'}) by-memory={by_memory} jobs={jobs}", file=sys.stderr)
    if a.print_jobs:
        print(jobs)


main()
