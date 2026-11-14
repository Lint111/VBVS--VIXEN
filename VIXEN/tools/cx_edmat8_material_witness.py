#!/usr/bin/env python3
"""Windowed editor witness for the procedural material-field carry."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import subprocess
import tempfile
import time
from pathlib import Path

from appflow_client import AppFlowClient


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


class Witness:
    def __init__(self, client: AppFlowClient, capture_dir: Path, log_path: Path):
        self.client = client
        self.capture_dir = capture_dir
        self.log_path = log_path
        catalog = client.list()
        self.actions = {item["name"]: item for item in catalog["actions"]}
        self.events: list[dict] = []

    def log_compile_count(self) -> int:
        if not self.log_path.exists():
            return 0
        return self.log_path.read_text(encoding="utf-8", errors="replace").count("[Compiler] Compiling stage:")

    def dispatch(self, name: str, **params) -> tuple[dict, int]:
        before = self.log_compile_count()
        reply = self.client.dispatch(actionId=self.actions[name]["id"], params=params)
        if reply["dispatchResult"]["name"] != "Ok":
            raise RuntimeError(f"{name} dispatch was {reply['dispatchResult']['name']}")
        presented = self.client.await_frame(action_id=reply["actionId"])
        after = self.log_compile_count()
        event = {"action": name, "params": params, "actionId": reply["actionId"],
                 "presentedFrame": presented["frame"], "compileLines": after - before}
        self.events.append(event)
        return event, after - before

    def shot(self, label: str, *, selection: dict | None = None) -> dict:
        shot = self.client.readback(selection or {"type": "fullFrame"}, format="png")
        path = self.capture_dir / f"{label}.png"
        path.write_bytes(base64.b64decode(shot["pixelsBase64"]))
        shot["path"] = str(path)
        return shot

    def changed_within(self, label: str, baseline_hash: int, baseline_frame: int, limit: int = 4) -> dict:
        first = None
        for _ in range(limit):
            current = self.shot(label)
            if current["hash"] != baseline_hash:
                first = current
                break
            self.client.await_frame(after_frame=current["frame"])
        if first is None:
            raise RuntimeError(f"{label} did not become visible within {limit} frames")
        first["framesFromBaseline"] = first["frame"] - baseline_frame
        return first

    def material_probe(self, points: list[dict], request_id: str) -> dict:
        return self.client.call("appflow.material_probe", {"points": points}, request_id=request_id)

    def scan_materials(self, label: str, x0: int, x1: int, y0: int, y1: int, step: int = 4) -> dict:
        points = [{"x": x, "y": y} for y in range(y0, y1 + 1, step) for x in range(x0, x1 + 1, step)]
        all_points = []
        width = 0
        height = 0
        for chunk_index in range(0, len(points), 64):
            chunk = points[chunk_index:chunk_index + 64]
            response = self.material_probe(chunk, f"probe-{label}-{chunk_index // 64}")
            width = response.get("width", width)
            height = response.get("height", height)
            all_points.extend(response["points"])
        result = {"width": width, "height": height, "step": step, "points": all_points}
        (self.capture_dir / f"{label}-material-probe.json").write_text(json.dumps(result, indent=2) + "\n")
        return result

    @staticmethod
    def pick_points(scan: dict) -> dict:
        by_id: dict[int, list[tuple[int, int]]] = {}
        for point in scan["points"]:
            by_id.setdefault(int(point["materialId"]), []).append((int(point["x"]), int(point["y"])))
        if 5 not in by_id or 8 not in by_id:
            raise RuntimeError(f"expected hull material IDs 5 and 8, got {sorted(by_id)}")
        base = by_id[5][0]
        bulge = by_id[8][0]
        pair = None
        positions = { (int(p["x"]), int(p["y"])): int(p["materialId"]) for p in scan["points"] }
        step = int(scan.get("step", 8))
        for x, y in by_id[5]:
            for nx, ny in ((x + step, y), (x - step, y), (x, y + step), (x, y - step)):
                if positions.get((nx, ny)) == 8:
                    pair = ((x, y), (nx, ny))
                    break
            if pair:
                break
        if pair is None:
            raise RuntimeError("could not locate adjacent material boundary")
        return {"body": base, "bulge": bulge, "boundaryBase": pair[0], "boundaryBulge": pair[1]}

    def run(self, width: int, height: int, editor: Path, pack: Path) -> dict:
        descriptor = self.capture_dir / "appflow-session.json"
        descriptor.unlink(missing_ok=True)
        env = os.environ.copy()
        env.update({
            "VIXEN_APPFLOW_DESCRIPTOR": str(descriptor),
            "VIXEN_EDITOR_PACK": str(pack),
            "VIXEN_EDITOR_PACK_SCHEMA": str(pack / "schemas.json"),
            "VIXEN_EDITOR_TEST_CAMERA": "top-down",
            "VIXEN_HIDDEN_WINDOW": "0",
            "VIXEN_WINDOW_WIDTH": str(width),
            "VIXEN_WINDOW_HEIGHT": str(height),
            "VIXEN_EXIT_AFTER_FRAMES": "10000000",
        })
        log_path = self.capture_dir / "editor.log"
        with log_path.open("w", encoding="utf-8") as log:
            process = subprocess.Popen([str(editor)], stdout=log, stderr=subprocess.STDOUT, env=env)
            try:
                deadline = time.monotonic() + 120
                while not descriptor.exists():
                    if process.poll() is not None:
                        raise RuntimeError(f"editor exited before AppFlow started: {process.returncode}")
                    if time.monotonic() > deadline:
                        raise RuntimeError("editor did not publish its AppFlow descriptor")
                    time.sleep(0.05)
                witness = Witness(AppFlowClient(descriptor, timeout=240), self.capture_dir, log_path)
                witness.client.await_frame(after_frame=5)
                witness.dispatch("OpenRecipe", recipeId="hullproof:hull")
                # The pack fixture intentionally starts with authored material references. Establish
                # the neutral authoring state explicitly for every layer before the first comparison
                # frame; this v5 fixture's stable layer ids are 1/2/3 for base/bulge/cut.
                for layer in range(1, 4):
                    witness.dispatch("ClearLayerMaterial", layer=layer)
                # The shipped bulge is nested at its default radius. Enlarge it before authoring the
                # two materials so both procedural surfaces are present in the same frame.
                witness.dispatch("SetParameter", parameterIndex=0, value=1.2)
                neutral = witness.shot("01-neutral")

                witness.dispatch("SetLayerMaterial", layer=1, material="core:hull_metal_major")
                witness.dispatch("SetLayerMaterial", layer=2, material="core:emissive_trim")
                two_materials = witness.shot("02-two-materials")
                scan = witness.scan_materials("02-two-materials", width // 2 - 100, width // 2 + 100,
                                             height // 2 - 100, height // 2 + 100, step=8)
                points = self.pick_points(scan)
                probes = witness.material_probe(
                    [{"x": x, "y": y} for x, y in points.values()], "probe-five-pixels")
                (self.capture_dir / "probe-five-pixels.json").write_text(json.dumps(
                    {"labels": list(points), "result": probes}, indent=2) + "\n")

                before_change = two_materials["hash"]
                change_event, change_compiles = witness.dispatch(
                    "SetLayerMaterial", layer=2, material="core:hull_metal_minor")
                change_visible = witness.changed_within("02b-bulge-change", before_change,
                                                       two_materials["frame"], limit=4)
                undo_event, undo_compiles = witness.dispatch("Undo")
                restored = witness.shot("02c-bulge-undo")

                before_save = witness.client.read()
                witness.dispatch("Save")
                saved_path = Path(before_save["pack"]["root"]) / "recipes" / "hull.vxd"
                saved_hash = sha256(saved_path)
                witness.dispatch("ReopenSaved")
                reopened_hash = sha256(saved_path)
                reopen_state = witness.client.read()

                parameter_base = restored["hash"]
                parameter_event, parameter_compiles = witness.dispatch("SetParameter", parameterIndex=0, value=0.8)
                parameter_visible = witness.changed_within("05-parameter-edit", parameter_base,
                                                           restored["frame"], limit=4)
                # The instance/material streams are ring-buffered. Let the updated parameter
                # occupy every in-flight slot before the cut-face probe, so the probe cannot
                # observe the preceding 1.2-radius frame while the stored coverage already
                # reflects 0.8.
                settled_frame = parameter_visible["frame"]
                for _ in range(3):
                    settled_frame = witness.client.await_frame(after_frame=settled_frame)["frame"]

                cut_event, _ = witness.dispatch("SetLayerMaterial", layer=3, material="core:hull_metal_minor")
                witness.client.await_frame(after_frame=cut_event["presentedFrame"])
                cut_own = witness.shot("03-cut-own-material")
                cut_scan = witness.scan_materials("03-cut-own-material", width // 2 - 100, width // 2 + 100,
                                                  height // 2 - 100, height // 2 + 100, step=8)
                cut_points = [p for p in cut_scan["points"] if int(p["materialId"]) == 6]
                if not cut_points:
                    raise RuntimeError("cut material id 6 did not appear in the HitRecord probe")
                cut_probe = witness.material_probe([{"x": cut_points[len(cut_points) // 2]["x"],
                                                     "y": cut_points[len(cut_points) // 2]["y"]}], "probe-cut-face")
                witness.dispatch("ClearLayerMaterial", layer=3)
                cut_without = witness.shot("04-cut-without-material")

                result = {
                    "resolution": [width, height], "windowed": True,
                    "images": {"neutral": neutral, "twoMaterials": two_materials,
                               "bulgeChange": change_visible, "bulgeUndo": restored,
                               "cutOwn": cut_own, "cutWithoutMaterial": cut_without,
                               "parameterEdit": parameter_visible},
                    "materialProbe": probes, "probeLabels": points, "cutFaceProbe": cut_probe,
                    "events": witness.events,
                    "compileCounts": {"bulgeChange": change_compiles, "bulgeUndo": undo_compiles,
                                      "parameterEdit": parameter_compiles},
                    "savedDocumentSha256": saved_hash, "reopenedDocumentSha256": reopened_hash,
                    "saveByteStable": saved_hash == reopened_hash,
                    "reopenedState": {"frame": reopen_state["frame"], "diagnostic": reopen_state["diagnostic"]},
                }
                (self.capture_dir / "witness.json").write_text(json.dumps(result, indent=2) + "\n")
                witness.client.quit()
                process.wait(timeout=120)
                return result
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=15)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=15)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--editor", type=Path, required=True)
    parser.add_argument("--pack", type=Path, required=True)
    parser.add_argument("--capture-dir", type=Path, required=True)
    parser.add_argument("--width", type=int, required=True)
    parser.add_argument("--height", type=int, required=True)
    args = parser.parse_args()
    args.capture_dir.mkdir(parents=True, exist_ok=True)
    # Pack mode opens the first recipe by id; the witness then explicitly reopens it through AppFlow.
    runner = Witness.__new__(Witness)
    runner.capture_dir = args.capture_dir
    result = runner.run(args.width, args.height, args.editor, args.pack)
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
