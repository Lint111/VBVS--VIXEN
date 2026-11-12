#!/usr/bin/env python3
"""sn-editorinput1 witness: drives the editor's AppFlow channel through the camera, key and typed-parameter
actions and records readback hashes + PNGs. A pure driver: every assertion compares values the editor itself
reported (dispatch results, undo depth, scene-image hashes, the editor log's parameter snapshots).

Run as a queued GPU job after `. <build>/vixen-gpu-env.sh` (same environment as tools/appflow_channel_gate.py):
  editor_input_witness.py --editor <vixen_editor> --document <copy of a .vxd> --out <evidence dir>
"""

from __future__ import annotations

import argparse
import base64
import json
import os
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from appflow_client import AppFlowClient  # noqa: E402

FULL = {"type": "fullFrame"}


class Witness:
    def __init__(self, client: AppFlowClient, out: Path, editor_log: Path):
        self.client = client
        self.out = out
        self.editor_log = editor_log
        self.actions = {a["name"]: a for a in client.list()["actions"]}
        self.rows: list[dict] = []

    def dispatch(self, name: str, **params):
        result = self.client.dispatch(actionId=self.actions[name]["id"], params=params)
        self.client.await_frame(action_id=result["actionId"])
        return result

    def chord(self, key: str, mods: list[str] | None = None):
        result = self.client.dispatch(chord={"key": key, "mods": mods or []})
        self.client.await_frame(action_id=result["actionId"])
        return result

    def shot(self, label: str) -> dict:
        """Scene-image readback after letting two frames present, saved as PNG; returns the raw-format hash."""
        time.sleep(0.4)
        raw = self.client.readback(FULL, format="raw")
        png = self.client.readback(FULL, format="png")
        data = png.get("pixelsBase64")
        path = self.out / f"{label}.png"
        if data:
            path.write_bytes(base64.b64decode(data))
        row = {"label": label, "hash": raw["hash"], "png": str(path), "pngHash": png["hash"],
               "frame": raw["frame"], "width": raw["width"], "height": raw["height"]}
        self.rows.append(row)
        return row

    def parameter_snapshots(self) -> list[list[float]]:
        text = self.editor_log.read_text(encoding="utf-8", errors="replace")
        return [[float(v) for v in m.group(1).split(",") if v] for m in re.finditer(r"\[EDITOR/snapshot\].*? cpu=\[([^\]]*)\]", text)]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--editor", required=True, type=Path)
    parser.add_argument("--document", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    report: dict = {}

    with tempfile.TemporaryDirectory(prefix="vixen-editorinput-") as temporary:
        descriptor = Path(temporary) / "session.json"
        env = os.environ.copy()
        env.update({"VIXEN_APPFLOW_DESCRIPTOR": str(descriptor), "VIXEN_EDITOR_OFFSCREEN_CAPTURE": "1",
                    "VIXEN_EDITOR_TEST_CAMERA": "top-down", "VIXEN_EXIT_AFTER_FRAMES": "100000"})
        editor_log = args.out / "editor.log"
        with editor_log.open("w", encoding="utf-8") as log:
            editor = subprocess.Popen([str(args.editor), str(args.document)], stdout=log,
                                      stderr=subprocess.STDOUT, env=env)
            try:
                deadline = time.monotonic() + 120
                while not descriptor.exists():
                    if editor.poll() is not None:
                        raise RuntimeError(f"editor exited before channel startup (exit {editor.returncode})")
                    if time.monotonic() > deadline:
                        raise RuntimeError("editor did not publish its channel descriptor")
                    time.sleep(0.05)
                client = AppFlowClient(descriptor)
                w = Witness(client, args.out, editor_log)
                catalog = client.list()
                keys = [{"id": k["id"], "action": next(a["name"] for a in catalog["actions"] if a["id"] == k["actionId"]),
                         "key": k["key"], "mods": k["mods"]} for k in catalog["keys"]]
                report["keyTable"] = keys
                report["actionCatalog"] = {name: [f'{p["name"]}:{p["type"]}' for p in a["params"]]
                                           for name, a in w.actions.items()
                                           if name in ("OrbitCamera", "PanCamera", "ZoomCamera", "FrameAsset", "SetViewAngle",
                                                       "SetParameter", "ResetParameter", "ScrubParameter", "Save")}

                # --- keys: plain S dispatches nothing, Ctrl+S saves ---------------------------------------
                plain_s = w.chord("S")
                ctrl_s = w.chord("S", ["Ctrl"])
                report["keys"] = {"S": plain_s["dispatchResult"]["name"], "Ctrl+S": ctrl_s["dispatchResult"]["name"]}

                base = w.shot("00-baseline")
                # --- camera as actions ---------------------------------------------------------------------
                depth0 = client.read()["undoDepth"]
                w.dispatch("OrbitCamera", dYaw=0.6, dPitch=-0.2)
                orbit = w.shot("01-orbit")
                w.dispatch("ZoomCamera", delta=4.0)
                zoom = w.shot("02-orbit-zoom")
                w.dispatch("PanCamera", dx=1.5, dy=-1.0)
                pan = w.shot("03-orbit-zoom-pan")
                depth1 = client.read()["undoDepth"]
                report["cameraIsNotADocumentEdit"] = {"undoDepthBefore": depth0, "undoDepthAfter": depth1}

                # FrameAsset restores centre+distance; yaw/pitch stay where the orbit left them, so the fit after
                # zoom+pan+Frame must equal the orbit-only shot (01), which still has the original fit.
                w.dispatch("ZoomCamera", delta=2.0)
                w.dispatch("PanCamera", dx=2.0, dy=2.0)
                moved = w.shot("05-moved")
                framed_key = w.chord("F")
                framed = w.shot("06-frame-asset-via-key-F")
                report["frameAsset"] = {"keyF": framed_key["dispatchResult"]["name"]}

                # view angle keys: Digit2 -> index 1; Digit1 -> index 0 restores yaw 0
                a1 = w.chord("Digit2")
                angle1 = w.shot("07-key-2-view-angle-1")
                w.dispatch("SetViewAngle", index=1)
                angle1_action = w.shot("08-action-SetViewAngle-1")
                a0 = w.chord("Digit1")
                angle0 = w.shot("09-key-1-view-angle-0")
                bad = client.dispatch(actionId=w.actions["SetViewAngle"]["id"], params={"index": 9})
                report["viewAngle"] = {"key2": a1["dispatchResult"]["name"], "key1": a0["dispatchResult"]["name"],
                                       "index9": bad["dispatchResult"]["name"]}

                # --- typed parameters ----------------------------------------------------------------------
                undo0 = client.read()["undoDepth"]
                p_before = w.shot("10-before-SetParameter")
                snap_before = len(w.parameter_snapshots())
                set_result = w.dispatch("SetParameter", parameterIndex=0, value=1.2)
                p_after = w.shot("11-after-SetParameter-0-1.2")
                snaps = w.parameter_snapshots()
                undo1 = client.read()["undoDepth"]
                clamped = w.dispatch("SetParameter", parameterIndex=0, value=1.0e9)
                snaps_clamped = w.parameter_snapshots()
                undo2 = client.read()["undoDepth"]
                reset = w.dispatch("ResetParameter", parameterIndex=0)
                snaps_reset = w.parameter_snapshots()
                p_reset = w.shot("12-after-ResetParameter-0")
                report["parameters"] = {
                    "setResult": set_result["dispatchResult"]["name"], "undoDepth": [undo0, undo1, undo2, client.read()["undoDepth"]],
                    "snapshotCountBefore": snap_before, "snapshotAfterSet": snaps[-1] if snaps else None,
                    "snapshotAfterOverRange": snaps_clamped[-1] if snaps_clamped else None,
                    "snapshotAfterReset": snaps_reset[-1] if snaps_reset else None,
                    "clampResult": clamped["dispatchResult"]["name"], "resetResult": reset["dispatchResult"]["name"]}
                undone = client.dispatch(actionId=w.actions["Undo"]["id"], params={})
                client.await_frame(action_id=undone["actionId"])
                report["parameters"]["undoOnce"] = {"result": undone["dispatchResult"]["name"],
                                                    "undoDepth": undone["undoDepth"], "redoDepth": undone["redoDepth"]}

                hashes = {r["label"]: r["hash"] for r in w.rows}
                report["hashes"] = hashes
                report["checks"] = {
                    "orbit changes viewport": hashes["01-orbit"] != hashes["00-baseline"],
                    "zoom changes viewport": hashes["02-orbit-zoom"] != hashes["01-orbit"],
                    "pan changes viewport": hashes["03-orbit-zoom-pan"] != hashes["02-orbit-zoom"],
                    "undo depth unchanged by camera": depth0 == depth1,
                    "moved differs from orbit-only": hashes["05-moved"] != hashes["01-orbit"],
                    "FrameAsset (key F) restores the orbit-only fit": hashes["06-frame-asset-via-key-F"] == hashes["01-orbit"],
                    "key 2 changes the view": hashes["07-key-2-view-angle-1"] != hashes["06-frame-asset-via-key-F"],
                    "key 2 equals SetViewAngle(1)": hashes["07-key-2-view-angle-1"] == hashes["08-action-SetViewAngle-1"],
                    "SetParameter changes viewport": hashes["11-after-SetParameter-0-1.2"] != hashes["10-before-SetParameter"],
                    "ResetParameter restores viewport": hashes["12-after-ResetParameter-0"] == hashes["10-before-SetParameter"],
                }
                (args.out / "witness.json").write_text(json.dumps({"report": report, "rows": w.rows}, indent=2) + "\n")
                print(json.dumps(report, indent=2))
                return 0 if all(report["checks"].values()) else 1
            finally:
                if editor.poll() is None:
                    editor.terminate()
                    try:
                        editor.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        editor.kill()
                        editor.wait(timeout=10)


if __name__ == "__main__":
    raise SystemExit(main())
