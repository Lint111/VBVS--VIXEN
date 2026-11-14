#!/usr/bin/env python3
"""End-to-end R6 proof through the stdio MCP server and editor-owned AppFlow channel.

Run outside CTest (e.g. as a queued GPU job), first `. <build>/vixen-gpu-env.sh`: configure writes the same
Vulkan ICD, layer and library environment CTest gives GPU tests, without it the editor fails with
VK_ERROR_INCOMPATIBLE_DRIVER.
"""

from __future__ import annotations

import argparse
import json
import os
import select
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any


class McpProcess:
    def __init__(self, server: Path, descriptor: Path, log_path: Path):
        self._log = log_path.open("w", encoding="utf-8")
        self.process = subprocess.Popen(
            [sys.executable, str(server), "--descriptor", str(descriptor)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=self._log,
            text=True,
            bufsize=1,
        )
        self.next_id = 1
        self.transcript: list[dict[str, Any]] = []

    def close(self) -> None:
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
        self._log.close()

    def rpc(self, method: str, params: dict[str, Any] | None = None, *, notification: bool = False) -> dict[str, Any] | None:
        request: dict[str, Any] = {"jsonrpc": "2.0", "method": method}
        modern_params = dict(params or {})
        modern_params["_meta"] = {
            "io.modelcontextprotocol/protocolVersion": "2026-07-28",
            "io.modelcontextprotocol/clientInfo": {"name": "vixen-appflow-gate", "version": "1.0.0"},
            "io.modelcontextprotocol/clientCapabilities": {},
        }
        request["params"] = modern_params
        if not notification:
            request["id"] = self.next_id
            self.next_id += 1
        assert self.process.stdin is not None and self.process.stdout is not None
        self.process.stdin.write(json.dumps(request, separators=(",", ":"), ensure_ascii=False) + "\n")
        self.process.stdin.flush()
        if notification:
            return None
        ready, _, _ = select.select([self.process.stdout], [], [], 90)
        if not ready:
            raise RuntimeError(f"MCP server timed out on {method}")
        line = self.process.stdout.readline()
        if not line:
            raise RuntimeError(f"MCP server exited on {method}, exit={self.process.poll()}")
        response = json.loads(line)
        entry = {"request": request, "response": response}
        self.transcript.append(entry)
        if "error" in response:
            raise RuntimeError(f"MCP RPC {method} failed: {response['error']}")
        return response

    def tool(self, name: str, arguments: dict[str, Any] | None = None) -> dict[str, Any]:
        started = time.perf_counter_ns()
        response = self.rpc("tools/call", {"name": name, "arguments": arguments or {}})
        round_trip_us = (time.perf_counter_ns() - started) // 1000
        assert response is not None
        result = response["result"]
        entry = self.transcript[-1]
        if result.get("isError"):
            raise RuntimeError(f"MCP tool {name} failed: {result['content'][0]['text']}")
        metadata: dict[str, Any] | None = None
        if result.get("content") and result["content"][0].get("type") == "text":
            try:
                metadata = json.loads(result["content"][0]["text"])
            except json.JSONDecodeError:
                pass
        if metadata is not None:
            metadata["mcpRoundTripUs"] = round_trip_us
            entry["toolResult"] = metadata
            for block in result.get("content", [])[1:]:
                entry.setdefault("images", []).append({"type": block.get("type"), "mimeType": block.get("mimeType")})
        return metadata if metadata is not None else result


def mask_value(state: dict[str, Any]) -> int:
    for view in state["views"]:
        if view["noun"]["name"] == "EditorNouns_layerMask":
            return int(view["value"])
    raise AssertionError("generated layer-mask noun missing from appflow.read")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--editor", required=True, type=Path)
    parser.add_argument("--server", required=True, type=Path)
    parser.add_argument("--document", required=True, type=Path)
    parser.add_argument("--transcript", required=True, type=Path)
    parser.add_argument("--force-compile-failure", action="store_true",
                        help="force the next procedural preview compile to fail and verify the wait returns")
    args = parser.parse_args()

    args.transcript.parent.mkdir(parents=True, exist_ok=True)
    mcp: McpProcess | None = None
    editor_log = args.transcript.with_suffix(".editor.log")
    mcp_log = args.transcript.with_suffix(".mcp.log")
    with tempfile.TemporaryDirectory(prefix="vixen-appflow-gate-") as temporary:
        descriptor = Path(temporary) / "session.json"
        environment = os.environ.copy()
        environment.update({
            "VIXEN_APPFLOW_DESCRIPTOR": str(descriptor),
            "VIXEN_EDITOR_OFFSCREEN_CAPTURE": "1",
            "VIXEN_EDITOR_TEST_CAMERA": "top-down",
            "VIXEN_EXIT_AFTER_FRAMES": "100000",
        })
        if args.force_compile_failure:
            environment["VIXEN_EDITOR_FORCE_RECIPE_COMPILE_FAILURE"] = "1"
        with editor_log.open("w", encoding="utf-8") as output:
            editor = subprocess.Popen(
                [str(args.editor), str(args.document)],
                stdout=output,
                stderr=subprocess.STDOUT,
                env=environment,
            )
            try:
                deadline = time.monotonic() + 90
                while not descriptor.exists():
                    if editor.poll() is not None:
                        raise RuntimeError(f"editor exited before channel startup (exit {editor.returncode}); see {editor_log}")
                    if time.monotonic() >= deadline:
                        raise RuntimeError(f"editor did not publish its channel descriptor; see {editor_log}")
                    time.sleep(0.05)

                mcp = McpProcess(args.server, descriptor, mcp_log)
                discovery = mcp.rpc("server/discover")
                assert discovery
                discovery_result = discovery["result"]
                assert discovery_result["supportedVersions"] == ["2026-07-28"]
                assert discovery_result["_meta"]["io.modelcontextprotocol/serverInfo"]["name"] == "vixen-appflow"
                listed = mcp.tool("appflow.list")
                tools_response = mcp.rpc("tools/list", {})
                advertised = {tool["name"] for tool in tools_response["result"]["tools"]}
                expected_tools = {"appflow.list", "appflow.dispatch", "appflow.read", "appflow.subscribe",
                                  "appflow.await_frame", "appflow.wait_recipe_preview", "appflow.readback"}
                assert expected_tools <= advertised, f"missing MCP tools: {sorted(expected_tools - advertised)}"

                actions = {item["name"]: item for item in listed["actions"]}
                toggle_id = actions["ToggleLayer"]["id"]
                undo_id = actions["Undo"]["id"]
                redo_id = actions["Redo"]["id"]
                toggle_param = actions["ToggleLayer"]["params"][0]["name"]
                assert actions["ToggleLayer"]["params"][0]["type"] == "integer"
                parameter_up = actions["AdjustParameterUp"]
                parameter_up_param = parameter_up["params"][0]["name"]
                assert parameter_up["params"][0]["type"] == "integer"
                program_up = actions["EditProgramFieldUp"]
                program_up_param = program_up["params"][0]["name"]
                assert program_up["params"][0]["type"] == "integer"

                parameter_edits = []
                for _ in range(4):
                    edited = mcp.tool("appflow.dispatch", {
                        "actionId": parameter_up["id"],
                        "params": {parameter_up_param: 0},
                    })
                    assert edited["dispatchResult"]["name"] == "Ok", edited
                    assert isinstance(edited["dispatchResult"]["code"], int)
                    assert (edited["undoDepth"], edited["redoDepth"]) == (len(parameter_edits) + 1, 0)
                    parameter_edits.append(edited)
                    assert mcp.tool("appflow.await_frame", {"actionId": edited["actionId"]})["presented"]

                initial = mcp.tool("appflow.read")
                assert mask_value(initial) == 7, f"R6 start mask was {mask_value(initial)}, expected 7"
                assert (initial["undoDepth"], initial["redoDepth"]) == (4, 0)
                initial_small = mcp.tool("appflow.readback", {
                    "selection": {"type": "sceneInstance", "reference": {"type": "bodyInstance", "index": 0}},
                    "format": "raw",
                })

                if args.force_compile_failure:
                    failed_edit = mcp.tool("appflow.dispatch", {
                        "actionId": program_up["id"],
                        "params": {program_up_param: 0},
                    })
                    assert failed_edit["dispatchResult"]["name"] == "Ok", failed_edit
                    failed_wait = mcp.tool("appflow.wait_recipe_preview", {
                        "serial": failed_edit["recipePreviewSerial"], "timeoutMs": 30000,
                    })
                    assert failed_wait["ready"] and not failed_wait["ok"], failed_wait
                    transcript = {"result": "PASS", "forcedCompileFailure": failed_wait,
                                  "dispatch": failed_edit, "mcpTranscript": mcp.transcript}
                    args.transcript.write_text(json.dumps(transcript, indent=2, ensure_ascii=False) + "\n",
                                               encoding="utf-8")
                    print(json.dumps({"result": "PASS", "forcedCompileFailure": failed_wait},
                                     ensure_ascii=False, separators=(",", ":")))
                    return 0

                ready_fence_samples = []
                previous_shape_hash = initial_small["hash"]
                for edit_index in range(10):
                    edited_shape = mcp.tool("appflow.dispatch", {
                        "actionId": program_up["id"],
                        "params": {program_up_param: 0},
                    })
                    assert edited_shape["dispatchResult"]["name"] == "Ok", edited_shape
                    serial = int(edited_shape["recipePreviewSerial"])
                    waited = mcp.tool("appflow.wait_recipe_preview", {"serial": serial, "timeoutMs": 60000})
                    assert waited["ready"] and waited["ok"] and waited["serial"] == serial, waited
                    sample = mcp.tool("appflow.readback", {
                        "selection": {"type": "sceneInstance",
                                       "reference": {"type": "bodyInstance", "index": 0}},
                        "format": "raw", "recipePreviewSerial": serial,
                    })
                    assert sample["recipePreviewSerial"] == serial and sample["recipePreviewOk"], sample
                    assert sample["hash"] != previous_shape_hash, {
                        "edit": edit_index, "serial": serial, "sample": sample,
                        "previousHash": previous_shape_hash,
                    }
                    ready_fence_samples.append({"edit": edit_index + 1, "serial": serial,
                                                "frame": sample["frame"], "hash": sample["hash"]})
                    previous_shape_hash = sample["hash"]

                changed = mcp.tool("appflow.dispatch", {
                    "actionId": toggle_id,
                    "params": {toggle_param: 2},
                    "readback": {
                        "selection": {"type": "sceneInstance", "reference": {"type": "bodyInstance", "index": 0}},
                        "format": "png",
                    },
                })
                assert changed["dispatchResult"]["name"] == "Ok", changed
                assert isinstance(changed["dispatchResult"]["code"], int)
                presented = mcp.tool("appflow.await_frame", {"actionId": changed["actionId"]})
                assert presented["presented"] and presented["frame"] >= changed["frame"]
                toggled_small = changed["readback"]
                visibility_samples = [toggled_small]
                if toggled_small["hash"] == initial_small["hash"]:
                    scene_selection = {
                        "type": "sceneInstance",
                        "reference": {"type": "bodyInstance", "index": 0},
                    }
                    for _ in range(5):
                        sample = mcp.tool("appflow.readback", {
                            "selection": scene_selection,
                            "format": "png",
                        })
                        visibility_samples.append(sample)
                        if sample["hash"] != initial_small["hash"]:
                            break
                visible_sample = next(
                    (sample for sample in visibility_samples if sample["hash"] != initial_small["hash"]),
                    None,
                )
                visible = visible_sample is not None
                frames_until_visible = (
                    max(1, visible_sample["frame"] - changed["frame"] + 1) if visible else None
                )
                state_after_toggle = mcp.tool("appflow.read")
                assert mask_value(state_after_toggle) == 3
                assert (state_after_toggle["undoDepth"], state_after_toggle["redoDepth"]) == (15, 0)

                undone = mcp.tool("appflow.dispatch", {"actionId": undo_id, "params": {}})
                assert undone["dispatchResult"]["name"] == "Ok", undone
                assert isinstance(undone["dispatchResult"]["code"], int)
                assert mcp.tool("appflow.await_frame", {"actionId": undone["actionId"]})["presented"]
                state_after_undo = mcp.tool("appflow.read")
                assert mask_value(state_after_undo) == 7
                assert (state_after_undo["undoDepth"], state_after_undo["redoDepth"]) == (14, 1)

                redone = mcp.tool("appflow.dispatch", {"actionId": redo_id, "params": {}})
                assert redone["dispatchResult"]["name"] == "Ok", redone
                assert isinstance(redone["dispatchResult"]["code"], int)
                assert mcp.tool("appflow.await_frame", {"actionId": redone["actionId"]})["presented"]
                state_after_redo = mcp.tool("appflow.read")
                assert mask_value(state_after_redo) == 3
                assert (state_after_redo["undoDepth"], state_after_redo["redoDepth"]) == (15, 0)

                events = mcp.tool("appflow.subscribe", {"after": 0})
                assert any(event["type"] == "view.reconcile" for event in events["events"]), events

                # E18: one labelled undo group is one undo step; nested and unbalanced calls are refused.
                depth_before_group = state_after_redo["undoDepth"]
                opened = mcp.tool("appflow.begin_group", {"label": "gate: three parameter steps"})
                assert opened["group"]["result"] == "Ok", opened
                nested = mcp.tool("appflow.begin_group", {"label": "nested"})
                assert nested["group"]["result"] == "NestedGroupRefused", nested
                for _ in range(3):
                    grouped_edit = mcp.tool("appflow.dispatch", {
                        "actionId": parameter_up["id"], "params": {parameter_up_param: 0}})
                    assert grouped_edit["dispatchResult"]["name"] == "Ok", grouped_edit
                closed = mcp.tool("appflow.end_group")
                assert closed["group"]["result"] == "Ok", closed
                assert closed["undoDepth"] == depth_before_group + 1, closed
                unbalanced = mcp.tool("appflow.end_group")
                assert unbalanced["group"]["result"] == "NoOpenGroupRefused", unbalanced
                grouped_undo = mcp.tool("appflow.dispatch", {"actionId": undo_id, "params": {}})
                assert (grouped_undo["undoDepth"], grouped_undo["redoDepth"]) == (depth_before_group, 1), grouped_undo
                group_events = [event["type"] for event in mcp.tool("appflow.subscribe", {"after": 0})["events"]]
                assert "group.begin" in group_events and "group.end" in group_events, group_events

                # E15: an invalid edit is visible as a typed diagnostic (status line text, read, event).
                bad = mcp.tool("appflow.dispatch", {"actionId": toggle_id, "params": {toggle_param: 99}})
                assert bad["dispatchResult"]["name"] == "Ok", bad
                diagnosed = mcp.tool("appflow.read")
                assert diagnosed["diagnostic"]["code"] == "InvalidLayerIndex", diagnosed["diagnostic"]
                diagnostic_events = [event for event in mcp.tool("appflow.subscribe", {"after": 0})["events"]
                                     if event["type"] == "diagnostic"]
                assert diagnostic_events and diagnostic_events[-1]["detail"]["code"] == "InvalidLayerIndex", diagnostic_events

                # Chord grammar: a chord listed by appflow.list dispatches exactly as listed.
                undo_chord = next(chord for chord in listed["chords"] if chord["actionId"] == undo_id)
                assert undo_chord["mods"] == ["Ctrl"] and isinstance(undo_chord["mods"], list), undo_chord
                depth_before_chord = diagnosed["undoDepth"]
                chorded = mcp.tool("appflow.dispatch", {
                    "chord": {"key": undo_chord["key"], "mods": undo_chord["mods"]}})
                assert chorded["dispatchResult"]["name"] == "Ok", chorded
                assert chorded["undoDepth"] == depth_before_chord - 1, chorded

                # E15: the runtime publishes on the engine bus and the channel mirrors it.
                navigated = mcp.tool("appflow.navigate", {"edge": "ToSettings"})
                assert navigated["flowState"]["name"] == "Settings", navigated
                returned = mcp.tool("appflow.dispatch", {"selector": "back-button"})
                assert returned["dispatchResult"]["name"] == "Ok" and returned["flowState"]["name"] == "Editing", returned
                edge_events = [event for event in mcp.tool("appflow.subscribe", {"after": 0})["events"]
                               if event["type"] == "flow.edge"]
                assert [event["detail"]["to"] for event in edge_events] == ["Settings", "Editing"], edge_events
                region = mcp.tool("appflow.readback", {
                    "selection": {"type": "sceneInstance", "reference": {"type": "bodyInstance", "index": 0}},
                    "format": "raw",
                })
                full = mcp.tool("appflow.readback", {"selection": {"type": "fullFrame"}, "format": "raw"})
                small_width = min(64, int(full["nativeWidth"]))
                small_height = min(64, int(full["nativeHeight"]))
                small = mcp.tool("appflow.readback", {
                    "selection": {"type": "screenRect", "x": 0, "y": 0,
                                  "width": small_width, "height": small_height},
                    "format": "raw",
                })
                assert full["width"] == full["nativeWidth"] and full["height"] == full["nativeHeight"]
                assert region["width"] <= full["width"] and region["height"] <= full["height"]
                assert (small["width"], small["height"]) == (small_width, small_height)
                assert region["hash"] and small["hash"] and full["hash"]

                paused = mcp.tool("appflow.pause")
                assert paused["paused"]
                stepped = mcp.tool("appflow.step", {"ticks": 2})
                stepped_frame = mcp.tool("appflow.await_frame", {"afterFrame": stepped["targetFrame"] - 1})
                assert stepped_frame["presented"]
                assert not mcp.tool("appflow.run")["paused"]

                transcript = {
                    "result": "PASS",
                    "maskTrail": [mask_value(initial), mask_value(state_after_toggle),
                                  mask_value(state_after_undo), mask_value(state_after_redo)],
                    "undoRedo": [
                        {"undoDepth": initial["undoDepth"], "redoDepth": initial["redoDepth"]},
                        {"undoDepth": state_after_toggle["undoDepth"], "redoDepth": state_after_toggle["redoDepth"]},
                        {"undoDepth": state_after_undo["undoDepth"], "redoDepth": state_after_undo["redoDepth"]},
                        {"undoDepth": state_after_redo["undoDepth"], "redoDepth": state_after_redo["redoDepth"]},
                    ],
                    "visualChangeVisible": visible,
                    "framesUntilVisible": frames_until_visible,
                    "visibilityProbe": [
                        {"frame": sample["frame"], "hash": sample["hash"]}
                        for sample in visibility_samples
                    ],
                    "mcpRoundTripUs": {
                        "dispatchWithReadback": changed["mcpRoundTripUs"],
                        "smallRegion": small["mcpRoundTripUs"],
                        "fullFrame": full["mcpRoundTripUs"],
                    },
                    "sceneRegion": {key: toggled_small[key] for key in (
                        "frame", "x", "y", "width", "height", "transferBytes", "fullImageTransferFallback",
                        "submitCpuUs", "mapCropCpuUs", "encodeHashCpuUs", "frameTimeImpactUs",
                        "readbackRoundTripUs", "hash")},
                    "smallRegionCost": {key: small[key] for key in (
                        "width", "height", "transferBytes", "fullImageTransferFallback", "submitCpuUs",
                        "mapCropCpuUs", "encodeHashCpuUs", "frameTimeImpactUs", "readbackRoundTripUs")},
                    "fullFrameCost": {key: full[key] for key in (
                        "width", "height", "transferBytes", "fullImageTransferFallback", "submitCpuUs",
                        "mapCropCpuUs", "encodeHashCpuUs", "frameTimeImpactUs", "readbackRoundTripUs")},
                    "smallRegion": {"width": small["width"], "height": small["height"],
                                    "transferBytes": small["transferBytes"], "hash": small["hash"]},
                    "parameterEditCount": len(parameter_edits),
                    "readyFenceSamples": ready_fence_samples,
                    "mcpTranscript": mcp.transcript,
                }
                args.transcript.write_text(json.dumps(transcript, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
                print(json.dumps({key: value for key, value in transcript.items() if key != "mcpTranscript"},
                                 ensure_ascii=False, separators=(",", ":")))
                return 0
            finally:
                if mcp:
                    mcp.close()
                if editor.poll() is None:
                    editor.terminate()
                    try:
                        editor.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        editor.kill()
                        editor.wait(timeout=10)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"AppFlow channel gate failed: {error}", file=sys.stderr)
        raise
