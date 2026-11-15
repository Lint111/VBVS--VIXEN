#!/usr/bin/env python3
"""Capture R872.13 default-material set/clear/undo/save/reopen through AppFlow."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import re
import subprocess
import time
from pathlib import Path

from appflow_client import AppFlowClient


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--editor", type=Path, required=True)
    parser.add_argument("--pack", type=Path, required=True)
    parser.add_argument("--capture-dir", type=Path, required=True)
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--capture-any-coverage", action="store_true")
    args = parser.parse_args()
    args.capture_dir.mkdir(parents=True, exist_ok=True)
    descriptor = args.capture_dir / "appflow-session.json"
    descriptor.unlink(missing_ok=True)
    log_path = args.capture_dir / "editor.log"
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
    events: list[dict] = []
    captures: dict[str, dict] = {}
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
            actions = {item["name"]: item for item in client.list()["actions"]}
            coverage_cursor = 0

            def dispatch(name: str, **params) -> dict:
                reply = client.dispatch(actionId=actions[name]["id"], params=params)
                if reply["dispatchResult"]["name"] != "Ok":
                    raise RuntimeError(f"{name} dispatch was {reply['dispatchResult']['name']}")
                presented = client.await_frame(action_id=reply["actionId"])
                event = {"action": name, "params": params, "frame": presented["frame"],
                         "dispatchResult": reply.get("dispatchResult"),
                         "state": client.read()}
                events.append(event)
                (args.capture_dir / "dispatch-events.json").write_text(
                    json.dumps(events, indent=2) + "\n", encoding="utf-8")
                return event

            def wait_coverage(required: dict[int, int], timeout: float = 180) -> str:
                nonlocal coverage_cursor
                pattern = re.compile(r"total=(\d+)(.*)")
                deadline = time.monotonic() + timeout
                while time.monotonic() < deadline:
                    lines = [line for line in log_path.read_text(encoding="utf-8", errors="replace").splitlines()
                             if "[EDITOR/material-coverage]" in line]
                    if len(lines) > coverage_cursor:
                        for line in lines[coverage_cursor:]:
                            match = pattern.search(line)
                            if not match or int(match.group(1)) != 401:
                                continue
                            counts = {int(slot): int(count) for slot, count in
                                      re.findall(r"slot(\d+)=(\d+)", match.group(2))}
                            if args.capture_any_coverage or all(counts.get(slot) == count for slot, count in required.items()):
                                coverage_cursor = len(lines)
                                return match.group(0)
                        coverage_cursor = len(lines)
                    time.sleep(0.25)
                raise RuntimeError(f"coverage did not reach {required}")

            def capture(label: str, required: dict[int, int]) -> None:
                coverage = wait_coverage(required)
                settled = client.await_frame(after_frame=client.read()["frame"] + 10)
                shot = client.readback({"type": "fullFrame"}, format="png")
                image = args.capture_dir / f"{label}.png"
                image.write_bytes(base64.b64decode(shot["pixelsBase64"]))
                captures[label] = {
                    "image": str(image), "frame": shot["frame"], "hash": shot["hash"],
                    "settleFrame": settled["frame"], "coverage": coverage,
                    "state": client.read(),
                }

            client.await_frame(after_frame=5)
            dispatch("OpenRecipe", recipeId="hullproof:hull")
            for layer in (1, 2, 3):
                dispatch("ClearLayerMaterial", layer=layer)
            dispatch("SetParameter", parameterIndex=0, value=1.2)
            dispatch("ToggleLayer", layerIndex=1)
            dispatch("ToggleLayer", layerIndex=2)
            dispatch("ToggleLayer", layerIndex=1)
            dispatch("SetLayerMaterial", layer=1, material="core:hull_metal_major")
            # Seed and then remove the bulge override so the witness exercises the exact
            # authored shape: base=A, bulge has no material, document default=B.
            dispatch("SetLayerMaterial", layer=2, material="core:emissive_trim")
            dispatch("ClearLayerMaterial", layer=2)

            dispatch("SetDocumentDefaultMaterial", material="core:emissive_trim")
            capture("01-default-set", {5: 254, 8: 147})

            dispatch("SetDocumentDefaultMaterial", material="")
            capture("02-default-cleared", {5: 254, 0: 147})

            dispatch("Undo")
            capture("03-default-undo-set", {5: 254, 8: 147})

            dispatch("Redo")
            capture("04-default-redo-cleared", {5: 254, 0: 147})

            dispatch("SetDocumentDefaultMaterial", material="core:emissive_trim")
            capture("05-default-before-save", {5: 254, 8: 147})
            before = client.read()
            dispatch("Save")
            saved_path = Path(before["pack"]["root"]) / "recipes" / "hull.vxd"
            saved_hash = sha256(saved_path)
            dispatch("ReopenSaved")
            reopened_hash = sha256(saved_path)
            capture("06-default-after-reopen", {5: 254, 8: 147})

            result = {
                "resolution": [args.width, args.height],
                "events": events,
                "captures": captures,
                "savedDocument": str(saved_path),
                "savedDocumentSha256": saved_hash,
                "reopenedDocumentSha256": reopened_hash,
                "saveByteStable": saved_hash == reopened_hash,
                "materialDefaultActionId": actions["SetDocumentDefaultMaterial"]["id"],
            }
            (args.capture_dir / "witness.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
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
