#!/usr/bin/env python3
"""Drives a live vixen_editor through its AppFlow agent channel and writes the capture fixture.

This replaces the retired VIXEN_EDITOR_SCRIPT frame timeline (R468.4). Every edit is an ordinary
channel dispatch (actionId, selector or navigate), every capture is a channel readback of the scene
target after the dispatched action has been presented and a settle window has passed, and the state
trail the test asserts is read back from the editor over the same channel.

Outputs under --capture-dir:
  run_editor_script.log        the editor's own log (stdout/stderr)
  appflow_state_trail.log      one line per step: "[CHANNEL/state] <op> mask=<m> undoDepth=<u> redoDepth=<r> result=<name>"
  editor_capture_<label>.png   labels 5, 25, 45, 75, 105 keep the names the file-assertion tests read

The editor must be started offscreen (VIXEN_EDITOR_OFFSCREEN_CAPTURE=1) by the caller's environment.
"""

from __future__ import annotations

import argparse
import base64
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from appflow_client import AppFlowClient  # noqa: E402

SETTLE_FRAMES = 15  # presented frames between an edit and its capture (scene residency catches up)
CHANGE_POLL_FRAMES = 5  # presented frames between re-reads while waiting for an edit to reach the pixels
CHANGE_TIMEOUT_SECONDS = 180  # an edit that never changes the image is captured as it is; the test then reports it


class Producer:
    def __init__(self, client: AppFlowClient, capture_dir: Path):
        self.client = client
        self.capture_dir = capture_dir
        self.trail = (capture_dir / "appflow_state_trail.log").open("w", encoding="utf-8")
        catalog = client.list()
        self.actions = {item["name"]: item for item in catalog["actions"]}
        self.edges = {item["id"]: item for item in catalog["transitions"]}

    def mask(self, state: dict) -> int:
        for view in state["views"]:
            if view["noun"]["name"] == "EditorNouns_layerMask":
                return int(view["value"])
        raise RuntimeError("generated layer-mask noun missing from appflow.read")

    def record(self, op: str, result: str, state: dict | None = None) -> None:
        state = state or self.client.read()
        self.trail.write(f"[CHANNEL/state] {op} mask={self.mask(state)} undoDepth={state['undoDepth']} "
                         f"redoDepth={state['redoDepth']} result={result}\n")
        self.trail.flush()

    def settle(self, frames: int = SETTLE_FRAMES) -> None:
        self.client.await_frame(after_frame=self.client.read()["frame"] + frames)

    def dispatch(self, op: str, **kwargs) -> None:
        reply = self.client.dispatch(**kwargs)
        name = reply["dispatchResult"]["name"]
        if name != "Ok":
            raise RuntimeError(f"{op} dispatch was {name}")
        self.client.await_frame(action_id=reply["actionId"])
        self.record(op, name)

    def action(self, op: str, name: str, **params) -> None:
        self.dispatch(op, actionId=self.actions[name]["id"], params=params)

    def capture(self, label: int, differs_from: int | None = None) -> int:
        """Reads the scene target; with differs_from, keeps reading (bounded) until the image changed,
        because a structural edit reaches the pixels only after the recipe pipeline has caught up."""
        deadline = time.monotonic() + CHANGE_TIMEOUT_SECONDS
        while True:
            shot = self.client.readback({"type": "fullFrame"}, format="png")
            if differs_from is None or shot["hash"] != differs_from or time.monotonic() > deadline:
                break
            self.client.await_frame(after_frame=shot["frame"] + CHANGE_POLL_FRAMES)
        (self.capture_dir / f"editor_capture_{label}.png").write_bytes(base64.b64decode(shot["pixelsBase64"]))
        return int(shot["hash"])

    def run(self) -> None:
        self.client.await_frame(after_frame=5)
        baseline = self.capture(5)
        for _ in range(4):
            self.action("parameter_up", "AdjustParameterUp", parameterIndex=0)
        self.settle()
        edited = self.capture(25, differs_from=baseline)
        self.action("toggle", "ToggleLayer", layerIndex=2)
        self.settle()
        cut = self.capture(45, differs_from=edited)
        self.action("undo", "Undo")
        self.settle()
        self.capture(75, differs_from=cut)
        self.action("redo", "Redo")
        self.settle()
        self.capture(105, differs_from=edited)
        self.action("save", "Save")
        reply = self.client.navigate("ToSettings")
        self.trail.write(f"[CHANNEL/state] settings flowState={reply['flowState']['name']}\n")
        self.dispatch("back", selector="back-button")
        after = self.client.read()
        self.trail.write(f"[CHANNEL/state] afterBack={after['flowState']['id']}\n")
        self.trail.flush()
        self.client.quit()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--editor", required=True, type=Path)
    parser.add_argument("--document", required=True, type=Path)
    parser.add_argument("--capture-dir", required=True, type=Path)
    args = parser.parse_args()
    args.capture_dir.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="vixen-editor-capture-") as temporary:
        descriptor = Path(temporary) / "session.json"
        environment = os.environ.copy()
        environment.update({
            "VIXEN_APPFLOW_DESCRIPTOR": str(descriptor),
            "VIXEN_EDITOR_CAPTURE_DIR": str(args.capture_dir),
            "VIXEN_EXIT_AFTER_FRAMES": "10000000",
        })
        with (args.capture_dir / "run_editor_script.log").open("w", encoding="utf-8") as log:
            editor = subprocess.Popen([str(args.editor), str(args.document)], stdout=log,
                                      stderr=subprocess.STDOUT, env=environment)
            finished = False
            try:
                deadline = time.monotonic() + 120
                while not descriptor.exists():
                    if editor.poll() is not None:
                        raise RuntimeError(f"editor exited before its channel started (exit {editor.returncode})")
                    if time.monotonic() > deadline:
                        raise RuntimeError("editor did not publish its channel descriptor within 120 s")
                    time.sleep(0.05)
                Producer(AppFlowClient(descriptor, timeout=240), args.capture_dir).run()
                finished = True
            finally:
                if finished:
                    try:
                        editor.wait(timeout=120)  # appflow.quit asked for an orderly shutdown
                    except subprocess.TimeoutExpired:
                        pass
                if editor.poll() is None:
                    editor.terminate()
                    try:
                        editor.wait(timeout=15)
                    except subprocess.TimeoutExpired:
                        editor.kill()
                        editor.wait(timeout=15)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"editor capture producer failed: {error}", file=sys.stderr)
        raise
