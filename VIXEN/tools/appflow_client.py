#!/usr/bin/env python3
"""Small synchronous client for an editor-owned AppFlow JSON-lines session."""

from __future__ import annotations

import json
import os
import socket
from pathlib import Path
from typing import Any


class AppFlowError(RuntimeError):
    pass


class AppFlowClient:
    def __init__(self, descriptor: str | os.PathLike[str], timeout: float = 45.0):
        self.descriptor_path = Path(descriptor)
        self.timeout = timeout
        try:
            self.descriptor = json.loads(self.descriptor_path.read_text(encoding="utf-8"))
        except OSError as exc:
            raise AppFlowError(f"cannot read AppFlow descriptor {self.descriptor_path}: {exc}") from exc
        if self.descriptor.get("protocol") != "vixen-appflow-jsonl/1":
            raise AppFlowError("unsupported AppFlow channel descriptor")

    @classmethod
    def from_environment(cls, timeout: float = 45.0) -> "AppFlowClient":
        path = os.environ.get("VIXEN_APPFLOW_DESCRIPTOR")
        if not path:
            raise AppFlowError("set VIXEN_APPFLOW_DESCRIPTOR or pass a descriptor path")
        return cls(path, timeout)

    def call(self, method: str, params: dict[str, Any] | None = None, *, request_id: Any = None) -> dict[str, Any]:
        request = {
            "id": request_id if request_id is not None else f"py-{id(self):x}-{os.urandom(4).hex()}",
            "token": self.descriptor["token"],
            "method": method,
            "params": params or {},
        }
        payload = (json.dumps(request, separators=(",", ":"), ensure_ascii=False) + "\n").encode("utf-8")
        with socket.create_connection(
            (self.descriptor["host"], int(self.descriptor["port"])), timeout=self.timeout
        ) as connection:
            connection.settimeout(self.timeout)
            connection.sendall(payload)
            response = bytearray()
            while b"\n" not in response:
                chunk = connection.recv(1024 * 1024)
                if not chunk:
                    raise AppFlowError("editor closed the AppFlow channel before replying")
                response.extend(chunk)
                if len(response) > 128 * 1024 * 1024:
                    raise AppFlowError("AppFlow response exceeds 128 MiB")
        try:
            envelope = json.loads(bytes(response).split(b"\n", 1)[0])
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise AppFlowError(f"invalid AppFlow response: {exc}") from exc
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
        params: dict[str, Any] = {}
        if action_id is not None:
            params["actionId"] = action_id
        elif after_frame is not None:
            params["afterFrame"] = after_frame
        else:
            raise ValueError("provide action_id or after_frame")
        return self.call("appflow.await_frame", params)

    def readback(
        self,
        selection: dict[str, Any],
        *,
        format: str = "png",
        action_id: str | int | None = None,
        frame: int | None = None,
    ) -> dict[str, Any]:
        params: dict[str, Any] = {"selection": selection, "format": format}
        if action_id is not None:
            params["actionId"] = action_id
        if frame is not None:
            params["frame"] = frame
        return self.call("appflow.readback", params)

    def pause(self) -> dict[str, Any]:
        return self.call("appflow.pause")

    def run(self) -> dict[str, Any]:
        return self.call("appflow.run")

    def step(self, ticks: int = 1) -> dict[str, Any]:
        return self.call("appflow.step", {"ticks": ticks})
