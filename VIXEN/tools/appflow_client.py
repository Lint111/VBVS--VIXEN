#!/usr/bin/env python3
"""Thin compatibility adapter for the kernel CodegenTool AppFlow transport."""

from __future__ import annotations

import atexit
import json
import os
import secrets
import subprocess
from pathlib import Path
from typing import Any


class AppFlowError(RuntimeError):
    pass


def _kernel_root() -> Path:
    configured = os.environ.get("YEROKET_KERNEL_ROOT") or os.environ.get("YEROKET_ROOT")
    candidates = [Path(configured)] if configured else []
    candidates.extend(Path.home() / path for path in ("projects/Yeroket-Fantasy", "Github/Yeroket-Fantasy"))
    for candidate in candidates:
        if candidate and (candidate / "Packages/com.yeroket.utility.kernel-framework/CodegenTool~/CodegenTool.csproj").is_file():
            return candidate
    raise AppFlowError("set YEROKET_KERNEL_ROOT or YEROKET_ROOT to a Yeroket checkout with CodegenTool~")


class AppFlowClient:
    """Preserve the Python API while the socket protocol lives in C#.

    One C# process is kept for the lifetime of this client, so editor witnesses
    do not pay a dotnet startup for every AppFlow request.
    """

    def __init__(self, descriptor: str | os.PathLike[str], timeout: float = 45.0):
        self.descriptor_path = Path(descriptor)
        self.timeout = timeout
        root = _kernel_root()
        dotnet = os.environ.get("YEROKET_DOTNET", "dotnet")
        dll = os.environ.get("YEROKET_CODEGEN_TOOL_DLL")
        if dll and Path(dll).is_file():
            command = [dotnet, "exec", dll]
        else:
            project = root / "Packages/com.yeroket.utility.kernel-framework/CodegenTool~/CodegenTool.csproj"
            command = [dotnet, "run", "--project", str(project), "-c", "Release", "-v:q"]
        command.extend(["--", "--appflow-client-stdio", "--descriptor", str(self.descriptor_path),
                        "--timeout-ms", str(max(1, int(timeout * 1000)))])
        try:
            self._process = subprocess.Popen(
                command,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
                text=True,
                encoding="utf-8",
                bufsize=1,
            )
        except OSError as exc:
            raise AppFlowError(f"cannot start C# AppFlow transport: {exc}") from exc
        atexit.register(self.close)

    @classmethod
    def from_environment(cls, timeout: float = 45.0) -> "AppFlowClient":
        path = os.environ.get("VIXEN_APPFLOW_DESCRIPTOR")
        if not path:
            raise AppFlowError("set VIXEN_APPFLOW_DESCRIPTOR or pass a descriptor path")
        return cls(path, timeout)

    def call(self, method: str, params: dict[str, Any] | None = None, *, request_id: Any = None) -> dict[str, Any]:
        if self._process.stdin is None or self._process.stdout is None:
            raise AppFlowError("C# AppFlow transport has no stdio channel")
        request = {
            "id": request_id if request_id is not None else f"py-{id(self):x}-{secrets.token_hex(4)}",
            "method": method,
            "params": params or {},
        }
        try:
            self._process.stdin.write(json.dumps(request, separators=(",", ":"), ensure_ascii=False) + "\n")
            self._process.stdin.flush()
            while True:
                line = self._process.stdout.readline()
                if not line:
                    raise AppFlowError(f"C# AppFlow transport exited with code {self._process.poll()}")
                try:
                    envelope = json.loads(line)
                except json.JSONDecodeError:
                    continue
                break
        except (OSError, ValueError) as exc:
            raise AppFlowError(f"AppFlow transport failed: {exc}") from exc
        if not envelope.get("ok"):
            raise AppFlowError(envelope.get("error", "unknown AppFlow error"))
        return envelope["result"]

    def list(self) -> dict[str, Any]:
        return self.call("appflow.list")

    def dispatch(self, *, request_id: Any = None, **dispatch: Any) -> dict[str, Any]:
        return self.call("appflow.dispatch", dispatch, request_id=request_id)

    def read(self, noun: int | str | None = None) -> dict[str, Any]:
        return self.call("appflow.read", {} if noun is None else {"noun": noun})

    def subscribe(self, after: int = 0) -> dict[str, Any]:
        return self.call("appflow.subscribe", {"after": after})

    def await_frame(self, *, action_id: str | int | None = None, after_frame: int | None = None) -> dict[str, Any]:
        if action_id is not None:
            params = {"actionId": action_id}
        elif after_frame is not None:
            params = {"afterFrame": after_frame}
        else:
            raise ValueError("provide action_id or after_frame")
        return self.call("appflow.await_frame", params)

    def wait_recipe_preview(self, serial: int, timeout_ms: int = 30000) -> dict[str, Any]:
        return self.call("appflow.wait_recipe_preview", {"serial": serial, "timeoutMs": timeout_ms})

    def readback(self, selection: dict[str, Any], *, format: str = "png", action_id: str | int | None = None,
                 frame: int | None = None, recipe_preview_serial: int | None = None,
                 timeout_ms: int = 30000) -> dict[str, Any]:
        params: dict[str, Any] = {"selection": selection, "format": format, "timeoutMs": timeout_ms}
        if action_id is not None:
            params["actionId"] = action_id
        if frame is not None:
            params["frame"] = frame
        if recipe_preview_serial is not None:
            params["recipePreviewSerial"] = recipe_preview_serial
        return self.call("appflow.readback", params)

    def input(self, **event: Any) -> dict[str, Any]:
        return self.call("appflow.input", event)

    def ui_reload(self, panel: str | None = None, *, readback: dict[str, Any] | None = None,
                  request_id: Any = None) -> dict[str, Any]:
        params: dict[str, Any] = {} if panel is None else {"panel": panel}
        if readback is not None:
            params["readback"] = readback
        return self.call("ui.reload", params, request_id=request_id)

    def appflow_reload(self) -> dict[str, Any]:
        return self.call("appflow.reload")

    def quit(self) -> dict[str, Any]:
        return self.call("appflow.quit")

    def navigate(self, edge: str) -> dict[str, Any]:
        return self.call("appflow.navigate", {"edge": edge})

    def begin_group(self, label: str) -> dict[str, Any]:
        return self.call("appflow.begin_group", {"label": label})

    def end_group(self) -> dict[str, Any]:
        return self.call("appflow.end_group")

    def pause(self) -> dict[str, Any]:
        return self.call("appflow.pause")

    def run(self) -> dict[str, Any]:
        return self.call("appflow.run")

    def step(self, ticks: int = 1) -> dict[str, Any]:
        return self.call("appflow.step", {"ticks": ticks})

    def close(self) -> None:
        process = getattr(self, "_process", None)
        if process is not None and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                process.kill()
