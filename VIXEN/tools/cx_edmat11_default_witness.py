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
                nonlocal coverage_cursor
                coverage_cursor = len([
                    line for line in log_path.read_text(encoding="utf-8", errors="replace").splitlines()
                    if "[EDITOR/material-coverage]" in line
                ])
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

            def wait_coverage(required: tuple[int, ...], timeout: float = 180) -> str:
                nonlocal coverage_cursor
                pattern = re.compile(r"total=(\d+)(.*)")
                deadline = time.monotonic() + timeout
                while time.monotonic() < deadline:
                    lines = [line for line in log_path.read_text(encoding="utf-8", errors="replace").splitlines()
                             if "[EDITOR/material-coverage]" in line]
                    if len(lines) > coverage_cursor:
                        for line in lines[coverage_cursor:]:
                            match = pattern.search(line)
                            if not match:
                                continue
                            total = int(match.group(1))
                            # An empty tuple is the neutral/no-material case: the
                            # coverage producer must report no categorical samples.
                            if not required and total == 0:
                                coverage_cursor = len(lines)
                                return match.group(0)
                            counts = {int(slot): int(count) for slot, count in
                                      re.findall(r"slot(\d+)=(\d+)", match.group(2))}
                            # The fixture has a 401-sample body in the original v5 pack and
                            # 450/475 samples in the v4 mask-7 pack used for this witness.  The
                            # invariant is 100% ownership by the required slot, not a fixture
                            # dependent sample count.
                            if total > 0 and all(counts.get(slot) == total for slot in required):
                                coverage_cursor = len(lines)
                                return match.group(0)
                        coverage_cursor = len(lines)
                    time.sleep(0.25)
                raise RuntimeError(f"coverage did not reach {required}")

            def capture(label: str, required: tuple[int, ...]) -> None:
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
            # Make the witness independent of the seed pack's default: start from no
            # document default, then set B explicitly for case (a).
            dispatch("SetDocumentDefaultMaterial", material="")
            for layer in (1, 2, 3):
                dispatch("ClearLayerMaterial", layer=layer)
            dispatch("SetParameter", parameterIndex=0, value=1.2)

            # Cases a/b: no layer carries a material, so the document default owns the whole
            # result and clearing it returns the neutral instance colour.
            dispatch("SetDocumentDefaultMaterial", material="core:emissive_trim")
            capture("01-all-none-default-b", (8,))

            dispatch("SetDocumentDefaultMaterial", material="")
            capture("02-all-none-default-cleared", ())

            dispatch("Undo")
            capture("03-default-undo-set", (8,))

            dispatch("Redo")
            capture("04-default-redo-cleared", ())

            # Case d: the cutter and body both have no material, so the cut wall also resolves to
            # the document default. Save/reopen this (case e) before the mixed-material case.
            dispatch("SetDocumentDefaultMaterial", material="core:emissive_trim")
            capture("05-all-none-default-b-cut-wall", (8,))
            before = client.read()
            dispatch("Save")
            saved_path = Path(before["pack"]["root"]) / "recipes" / "hull.vxd"
            saved_hash = sha256(saved_path)
            dispatch("ReopenSaved")
            reopened_hash = sha256(saved_path)
            capture("06-default-after-reopen", (8,))

            # Case c: base=A, bulge/cut have no material, default=B. The unmaterialed operands
            # are transparent; the composed result is entirely A, so B must not appear in coverage.
            dispatch("SetLayerMaterial", layer=1, material="core:hull_metal_major")
            capture("07-base-a-default-b", (5,))

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
