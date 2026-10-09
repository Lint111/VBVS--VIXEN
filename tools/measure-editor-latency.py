#!/usr/bin/env python3
"""R463 phase spans and first changed scene capture, using the configured editor fixture.

Each editor invocation uses the live box queue. Times describe CPU wall spans through
queue submission/present return, not physical display scanout or GPU execution time.
Capture readback is outside the measured frame span, but delays the following tick.
"""

import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import shlex
import struct
import subprocess
import zlib


REPO = Path(__file__).resolve().parents[1]
QUEUE = "/home/liory/.local/bin/with-test-lock.sh"
CASES = {
    "toggle": "toggle:2@30,toggle:2@60,undo@90,redo@120",
    "parameter": ",".join(["parameter_up:0@30"] * 4 + ["undo@60"] * 4 + ["redo@90"] * 4),
    "parameter_hidden": "parameter_up:0@30,undo@60,redo@90",
    "create": "create@30,undo@60,redo@90",
    "delete": "delete:2@30,undo@60,redo@90",
    "reorder": "move_up:2@30,undo@60,redo@90",
    "program_field": "program_up:2@30,undo@60,redo@90",
}
spec = importlib.util.spec_from_file_location("capture_pixels", REPO / "tools/compare-capture-pixels.py")
pixels = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pixels)


def contact_sheet(paths, output, load=pixels.decode_png):
    images = [load(path) for path in paths]
    width, height, _ = images[0]
    if any(image[:2] != (width, height) for image in images):
        raise ValueError("capture extents differ")
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for _, _, rgba in images:
            raw.extend(rgba[y * width * 4:(y + 1) * width * 4])

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    output.write_bytes(pixels.PNG_SIGNATURE
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width * len(images), height, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def analyse(directory):
    decoded = {}

    def load(path):
        if path not in decoded:
            decoded[path] = pixels.decode_png(path)
        return decoded[path]

    events = []
    for line in (directory / "run.log").read_text(errors="replace").splitlines():
        match = re.search(r"\[RECIPE/latency\] (.*)", line)
        if match:
            event = dict(part.split("=", 1) for part in match[1].split())
            for key in ("wall_us", "tick", "action"):
                if key in event:
                    event[key] = int(event[key])
            events.append(event)
    events.sort(key=lambda event: event["wall_us"])
    starts = {}
    spans = []
    for event in events:
        key = (event["phase"], event.get("tick"))
        if event["edge"] == "begin":
            if key in starts:
                raise ValueError(f"nested/unclosed span: {key}")
            starts[key] = event
        elif key in starts:
            start = starts.pop(key)
            spans.append({**start, "end_us": event["wall_us"],
                          "ms": (event["wall_us"] - start["wall_us"]) / 1000})
        else:
            raise ValueError(f"end without begin: {event}")
    if starts:
        raise ValueError(f"unclosed spans: {list(starts)}")
    live_ticks = [span["tick"] for span in spans
        if span["phase"] == "dispatch" and span.get("action") == -1]
    if live_ticks:
        raise ValueError(f"script capture contains live UI dispatch at ticks: {live_ticks}")
    frames = {span["tick"]: span for span in spans if span["phase"] == "frame"}
    results = []
    captures = sorted(directory.glob("editor_capture_*.png"), key=lambda path: int(path.stem.split("_")[-1]))
    dispatches = {}
    for span in (span for span in spans if span["phase"] == "dispatch"):
        key = (span["tick"], span["action"])
        if key not in dispatches:
            dispatches[key] = {**span, "count": 1}
        else:
            group = dispatches[key]
            group["end_us"] = span["end_us"]
            group["ms"] = (group["end_us"] - group["wall_us"]) / 1000
            group["count"] += 1
    for dispatch in dispatches.values():
        tick = dispatch["tick"]
        before = directory / f"editor_capture_{tick - 1}.png"
        reference = load(before)
        candidates = [path for path in captures if tick <= int(path.stem.split("_")[-1]) <= tick + 3]
        changed = next((path for path in candidates if load(path) != reference), None)
        changed_tick = int(changed.stem.split("_")[-1]) if changed else None
        settled = load(directory / f"editor_capture_{tick + 3}.png")
        first_settled = next((path for path in candidates if load(path) == settled), None) if settled != reference else None
        settled_tick = int(first_settled.stem.split("_")[-1]) if first_settled else None
        end = frames[changed_tick]["end_us"] if changed else frames[tick + 3]["end_us"]
        phases = {phase: sum(span["ms"] for span in spans if span["phase"] == phase
            and dispatch["wall_us"] <= span["wall_us"] and span["end_us"] <= end)
            for phase in ("flatten", "registry", "shader_request", "bake", "publish", "compile", "materialize", "render")}
        phase_ticks = {phase: sorted({frame_tick for span in spans if span["phase"] == phase
            and dispatch["wall_us"] <= span["wall_us"] and span["end_us"] <= end
            for frame_tick, frame in frames.items()
            if frame["wall_us"] <= span["wall_us"] and span["end_us"] <= frame["end_us"]})
            for phase in phases}
        phases["registry_exclusive"] = phases["registry"] - phases["shader_request"]
        # Render includes graph compilation/materialization; expose its exclusive remainder.
        phases["render_exclusive"] = phases["render"] - sum(span["ms"] for span in spans
            if span["phase"] in ("compile", "materialize") and any(render["phase"] == "render"
                and render["wall_us"] <= span["wall_us"] and span["end_us"] <= render["end_us"] <= end
                and render["wall_us"] >= dispatch["wall_us"] for render in spans))
        flatten = next((span for span in spans if span["phase"] == "flatten" and span.get("tick") == tick), None)
        readback = next((span for span in spans if span["phase"] == "readback" and span.get("tick") == changed_tick), None)
        results.append({"dispatch_tick": tick, "action": dispatch["action"], "dispatch_count": dispatch["count"],
            "first_changed_tick": changed_tick,
            "first_matching_settled_tick": settled_tick,
            "settled_match_frames": settled_tick - tick + 1 if first_settled else None,
            "presented_frames_to_change": changed_tick - tick + 1 if changed else None,
            "dispatch_to_frame_end_ms": (end - dispatch["wall_us"]) / 1000 if changed else None,
            "capture_completion_ms": (readback["end_us"] - dispatch["wall_us"]) / 1000 if readback else None,
            "dispatch_ms": dispatch["ms"],
            "dispatch_to_flatten_ms": (flatten["wall_us"] - dispatch["end_us"]) / 1000 if flatten else None,
            "dispatch_to_flatten_frames": flatten["tick"] - tick if flatten else None,
            "phase_ticks": phase_ticks,
            "phases_ms": phases})
        if changed:
            contact_sheet([before, directory / f"editor_capture_{tick}.png",
                directory / f"editor_capture_{tick + 1}.png", directory / f"editor_capture_{tick + 3}.png"],
                          directory / f"contact-{tick}.png", load=load)
    # Roundtrip compares settled scene images, separate from the first-change assertion.
    initial = directory / "editor_capture_29.png"
    results_roundtrip = {str(tick): (directory / f"editor_capture_{tick}.png").read_bytes() == initial.read_bytes()
        for tick in (63, 93, 123) if (directory / f"editor_capture_{tick}.png").exists()}
    first_edit = directory / "editor_capture_33.png"
    edit_identity = {str(tick): (directory / f"editor_capture_{tick}.png").read_bytes() == first_edit.read_bytes()
        for tick in (63, 93, 123) if (directory / f"editor_capture_{tick}.png").exists()}
    return {"edits": results, "initial_identity_at_tick": results_roundtrip,
            "first_edit_identity_at_tick": edit_identity,
            "frame_spans_ms": {str(tick): frame["ms"] for tick, frame in frames.items()}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=REPO / "VIXEN/build")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--agent", default="togglelatency")
    parser.add_argument("--case", choices=CASES, action="append")
    parser.add_argument("--analyse-only", action="store_true")
    parser.add_argument("--windowed", action="store_true")
    parser.add_argument("--display", default=":0", help="X11 display for the windowed witness")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if not args.analyse_only:
        catalogue = json.loads(subprocess.check_output(
            ["ctest", "--test-dir", str(args.build_dir.resolve()), "--show-only=json-v1"], text=True))
        fixture = next(test for test in catalogue["tests"] if test["name"] == "vixen_editor_capture_producer")
        properties = {prop["name"]: prop["value"] for prop in fixture["properties"]}
        command = shlex.split(fixture["command"][-1])[1:3]  # executable + staged document
    report = {}
    names = args.case or ([name for name in CASES if (output / name / "run.log").exists()]
        if args.analyse_only else CASES)
    if not names:
        raise ValueError(f"no captured cases in {output}")
    for name in names:
        directory = output / name
        if not args.analyse_only:
            directory.mkdir(parents=True, exist_ok=True)
            environment = dict(os.environ)
            environment.update(item.split("=", 1) for item in properties["ENVIRONMENT"])
            environment.pop("DISPLAY", None)
            environment.pop("WAYLAND_DISPLAY", None)
            environment.update(VIXEN_EDITOR_SCRIPT=CASES[name], VIXEN_EDITOR_LATENCY_TRACE="1",
                VIXEN_EDITOR_CAPTURE_DIR=str(directory), VIXEN_EXIT_AFTER_FRAMES="132",
                VIXEN_CACHE_DIR=str(REPO / ".tmp/editor-latency-cache" / output.name / name),
                VIXEN_EDITOR_CAPTURE_FRAMES=",".join(str(frame) for tick in (30, 60, 90, 120)
                    for frame in range(tick - 2, tick + 4)))
            if args.windowed:
                environment["DISPLAY"] = args.display
                environment.pop("VIXEN_EDITOR_OFFSCREEN_CAPTURE", None)
            with (directory / "run.log").open("w") as log:
                subprocess.run(["bash", QUEUE, "--agent", args.agent, "--resource", "test",
                    "--label", f"run2-latency-{output.name}-{name}", "--", "timeout", "180s", "bash", "-lc",
                    'exec "$@"', "editor-latency", *command], cwd=properties["WORKING_DIRECTORY"],
                    env=environment, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=7200)
        report[name] = analyse(directory)
        (output / "measurements.json").write_text(json.dumps(report, indent=2) + "\n")
        print(name, json.dumps(report[name]["edits"][0]), flush=True)


if __name__ == "__main__":
    main()
