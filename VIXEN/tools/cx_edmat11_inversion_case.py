#!/usr/bin/env python3
"""Capture one cx-edmat11 material-ownership inversion case through AppFlow."""

from __future__ import annotations

import argparse
import base64
import json
import os
import re
import subprocess
import time
from pathlib import Path

from appflow_client import AppFlowClient


CASES = {
    "a-one-box-major": [
        ("ClearLayerMaterial", {"layer": 1}),
        ("ClearLayerMaterial", {"layer": 2}),
        ("ClearLayerMaterial", {"layer": 3}),
        ("SetParameter", {"parameterIndex": 0, "value": 1.2}),
        ("ToggleLayer", {"layerIndex": 1}),
        ("ToggleLayer", {"layerIndex": 2}),
        ("SetLayerMaterial", {"layer": 1, "material": "core:hull_metal_major"}),
    ],
    "b-one-box-trim": [
        ("ClearLayerMaterial", {"layer": 1}),
        ("ClearLayerMaterial", {"layer": 2}),
        ("ClearLayerMaterial", {"layer": 3}),
        ("SetParameter", {"parameterIndex": 0, "value": 1.2}),
        ("ToggleLayer", {"layerIndex": 1}),
        ("ToggleLayer", {"layerIndex": 2}),
        ("SetLayerMaterial", {"layer": 1, "material": "core:emissive_trim"}),
    ],
    "c-smooth-major-trim": [
        ("ClearLayerMaterial", {"layer": 1}),
        ("ClearLayerMaterial", {"layer": 2}),
        ("ClearLayerMaterial", {"layer": 3}),
        ("SetParameter", {"parameterIndex": 0, "value": 1.2}),
        ("ToggleLayer", {"layerIndex": 1}),
        ("ToggleLayer", {"layerIndex": 2}),
        ("ToggleLayer", {"layerIndex": 1}),
        ("SetLayerMaterial", {"layer": 1, "material": "core:hull_metal_major"}),
        ("SetLayerMaterial", {"layer": 2, "material": "core:emissive_trim"}),
    ],
    "d-smooth-trim-major": [
        ("ClearLayerMaterial", {"layer": 1}),
        ("ClearLayerMaterial", {"layer": 2}),
        ("ClearLayerMaterial", {"layer": 3}),
        ("SetParameter", {"parameterIndex": 0, "value": 1.2}),
        ("ToggleLayer", {"layerIndex": 1}),
        ("ToggleLayer", {"layerIndex": 2}),
        ("ToggleLayer", {"layerIndex": 1}),
        ("SetLayerMaterial", {"layer": 1, "material": "core:emissive_trim"}),
        ("SetLayerMaterial", {"layer": 2, "material": "core:hull_metal_major"}),
    ],
    "e-union-major-trim": [
        ("ClearLayerMaterial", {"layer": 1}),
        ("ClearLayerMaterial", {"layer": 2}),
        ("ClearLayerMaterial", {"layer": 3}),
        ("SetParameter", {"parameterIndex": 0, "value": 1.2}),
        ("ToggleLayer", {"layerIndex": 1}),
        ("ToggleLayer", {"layerIndex": 2}),
        ("ToggleLayer", {"layerIndex": 1}),
        ("SetLayerMaterial", {"layer": 1, "material": "core:hull_metal_major"}),
        ("SetLayerMaterial", {"layer": 2, "material": "core:emissive_trim"}),
        ("SetLayerOp", {"layerIndex": 1, "op": "Union"}),
    ],
    "f-order-bulge-first": [
        ("ClearLayerMaterial", {"layer": 1}),
        ("ClearLayerMaterial", {"layer": 2}),
        ("ClearLayerMaterial", {"layer": 3}),
        ("SetParameter", {"parameterIndex": 0, "value": 1.2}),
        ("ToggleLayer", {"layerIndex": 1}),
        ("ToggleLayer", {"layerIndex": 2}),
        ("ToggleLayer", {"layerIndex": 1}),
        ("SetLayerMaterial", {"layer": 1, "material": "core:hull_metal_major"}),
        ("SetLayerMaterial", {"layer": 2, "material": "core:emissive_trim"}),
        ("MoveLayerUp", {"layerIndex": 1}),
    ],
}

POINTS = {
    "box": {"x": 600, "y": 305},
    "bulge": {"x": 655, "y": 315},
    "cut": {"x": 610, "y": 375},
}

COVERAGE = {
    "a-one-box-major": re.compile(r"total=386 slot5=386$"),
    "b-one-box-trim": re.compile(r"total=386 slot8=386$"),
    "c-smooth-major-trim": re.compile(r"total=401 slot5=254 slot8=147$"),
    "d-smooth-trim-major": re.compile(r"total=401 slot8=254 slot5=147$"),
    "e-union-major-trim": re.compile(r"total=401 slot5=254 slot8=147$"),
    "f-order-bulge-first": re.compile(r"total=401 slot5=254 slot8=147$"),
}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--case", choices=sorted(CASES), required=True)
    parser.add_argument("--editor", type=Path, required=True)
    parser.add_argument("--pack", type=Path, required=True)
    parser.add_argument("--capture-dir", type=Path, required=True)
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    args = parser.parse_args()
    args.capture_dir.mkdir(parents=True, exist_ok=True)
    descriptor = args.capture_dir / "appflow-session.json"
    descriptor.unlink(missing_ok=True)
    log_path = args.capture_dir / "editor.log"
    events: list[dict] = []
    env = os.environ.copy()
    env.update({
        "VIXEN_APPFLOW_DESCRIPTOR": str(descriptor),
        "VIXEN_EDITOR_PACK": str(args.pack),
        "VIXEN_EDITOR_PACK_SCHEMA": str(args.pack / "schemas.json"),
        "VIXEN_EDITOR_TEST_CAMERA": "top-down",
        "VIXEN_HIDDEN_WINDOW": "0",
        "VIXEN_WINDOW_WIDTH": str(args.width),
        "VIXEN_WINDOW_HEIGHT": str(args.height),
        "VIXEN_EXIT_AFTER_FRAMES": "10000000",
    })
    process = None
    try:
        with log_path.open("w", encoding="utf-8") as log:
            process = subprocess.Popen([str(args.editor)], stdout=log, stderr=subprocess.STDOUT, env=env)
            deadline = time.monotonic() + 120
            while not descriptor.exists():
                if process.poll() is not None:
                    raise RuntimeError(f"editor exited before AppFlow started: {process.returncode}")
                if time.monotonic() > deadline:
                    raise RuntimeError("editor did not publish its AppFlow descriptor")
                time.sleep(0.05)
            client = AppFlowClient(descriptor, timeout=240)
            catalog = client.list()
            actions = {item["name"]: item for item in catalog["actions"]}

            def dispatch(name: str, **params) -> None:
                reply = client.dispatch(actionId=actions[name]["id"], params=params)
                if reply["dispatchResult"]["name"] != "Ok":
                    raise RuntimeError(f"{name} dispatch was {reply['dispatchResult']['name']}")
                presented = client.await_frame(action_id=reply["actionId"])
                events.append({"action": name, "params": params, "frame": presented["frame"]})

            client.await_frame(after_frame=5)
            dispatch("OpenRecipe", recipeId="hullproof:hull")
            for name, params in CASES[args.case]:
                dispatch(name, **params)
            coverage_line = None
            coverage_deadline = time.monotonic() + 180
            while time.monotonic() < coverage_deadline:
                lines = log_path.read_text(encoding="utf-8", errors="replace").splitlines()
                coverage_lines = [line for line in lines if "[EDITOR/material-coverage]" in line]
                if coverage_lines:
                    candidate = coverage_lines[-1].split("[EDITOR/material-coverage] ", 1)[-1]
                    if COVERAGE[args.case].search(candidate):
                        coverage_line = candidate
                        break
                time.sleep(0.5)
            if coverage_line is None:
                raise RuntimeError(f"final material coverage was not published for {args.case}")
            settle = client.await_frame(after_frame=client.read()["frame"] + 10)
            shot = client.readback({"type": "fullFrame"}, format="png")
            image_path = args.capture_dir / f"{args.case}.png"
            image_path.write_bytes(base64.b64decode(shot["pixelsBase64"]))
            try:
                probes = client.call("appflow.material_probe", {"points": list(POINTS.values())}, request_id="inversion")
            except Exception as error:  # the current editor frame pin does not expose HitRecord
                probes = {"unavailable": str(error)}
            state = client.read()
            (args.capture_dir / "events.json").write_text(json.dumps(events, indent=2) + "\n", encoding="utf-8")
            (args.capture_dir / f"{args.case}.state.json").write_text(json.dumps(state, indent=2) + "\n", encoding="utf-8")
            (args.capture_dir / "material-probe.json").write_text(json.dumps({"labels": POINTS, "result": probes}, indent=2) + "\n", encoding="utf-8")
            (args.capture_dir / "run.json").write_text(json.dumps({
                "case": args.case,
                "image": str(image_path),
                "frame": shot["frame"],
                "hash": shot["hash"],
                "settleFrame": settle["frame"],
                "coverage": coverage_line,
                "state": {"frame": state["frame"], "diagnostic": state["diagnostic"]},
            }, indent=2) + "\n", encoding="utf-8")
            client.quit()
            process.wait(timeout=120)
    finally:
        if process is not None and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=15)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
