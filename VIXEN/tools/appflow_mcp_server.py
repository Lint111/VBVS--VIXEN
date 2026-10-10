#!/usr/bin/env python3
"""MCP stdio adapter for a running editor's AppFlow JSON-lines channel."""

from __future__ import annotations

import argparse
import base64
import json
import os
import sys
from typing import Any

from appflow_client import AppFlowClient, AppFlowError


TOOLS: list[dict[str, Any]] = [
    {
        "name": "appflow.list",
        "description": "Discover generated AppFlow actions, typed parameters, selectors, chords, views, and states.",
        "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
    },
    {
        "name": "appflow.dispatch",
        "description": "Dispatch through a generated action id, AppFlow selector, or generated key chord.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "actionId": {"oneOf": [{"type": "integer"}, {"type": "string"}]},
                "params": {"type": "object"},
                "selector": {"type": "string"},
                "readback": {
                    "type": "object",
                    "properties": {"selection": {"type": "object"}, "format": {"type": "string", "enum": ["png", "raw"]}},
                    "required": ["selection"],
                    "additionalProperties": False,
                },
                "chord": {
                    "type": "object",
                    "properties": {"key": {"type": "string"}, "mods": {"type": "array", "items": {"type": "string"}}},
                    "required": ["key"],
                },
            },
            "additionalProperties": False,
        },
    },
    {
        "name": "appflow.read",
        "description": "Read AppFlow flow state, undo/redo depth, or a generated readable view noun.",
        "inputSchema": {"type": "object", "properties": {"noun": {"oneOf": [{"type": "integer"}, {"type": "string"}]}}, "additionalProperties": False},
    },
    {
        "name": "appflow.subscribe",
        "description": "Return AppFlow action and per-frame view reconciliation events after a sequence number.",
        "inputSchema": {"type": "object", "properties": {"after": {"type": "integer", "minimum": 0}}, "additionalProperties": False},
    },
    {
        "name": "appflow.await_frame",
        "description": "Wait until the first presented frame after an action request or frame number.",
        "inputSchema": {
            "type": "object",
            "properties": {"actionId": {"oneOf": [{"type": "integer"}, {"type": "string"}]}, "afterFrame": {"type": "integer", "minimum": 0}},
            "additionalProperties": False,
        },
    },
    {
        "name": "appflow.readback",
        "description": "Read native-resolution pixels for a screen rectangle, selector, scene instance, entity, or full frame.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "selection": {"type": "object"},
                "format": {"type": "string", "enum": ["png", "raw"]},
                "actionId": {"oneOf": [{"type": "integer"}, {"type": "string"}]},
                "frame": {"type": "integer", "minimum": 0},
            },
            "required": ["selection"],
            "additionalProperties": False,
        },
    },
    {
        "name": "appflow.pause",
        "description": "Pause editor update ticks while the render loop continues to service control requests.",
        "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
    },
    {
        "name": "appflow.run",
        "description": "Resume editor update ticks.",
        "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
    },
    {
        "name": "appflow.step",
        "description": "Advance a paused editor by a bounded number of update ticks.",
        "inputSchema": {"type": "object", "properties": {"ticks": {"type": "integer", "minimum": 1, "maximum": 10000}}, "additionalProperties": False},
    },
]


def tool_content(name: str, arguments: dict[str, Any], client: AppFlowClient) -> dict[str, Any]:
    if name == "appflow.list":
        result = client.list()
    elif name == "appflow.dispatch":
        result = client.dispatch(**arguments)
    elif name == "appflow.read":
        result = client.read(arguments.get("noun"))
    elif name == "appflow.subscribe":
        result = client.subscribe(arguments.get("after", 0))
    elif name == "appflow.await_frame":
        result = client.await_frame(action_id=arguments.get("actionId"), after_frame=arguments.get("afterFrame"))
    elif name == "appflow.readback":
        result = client.readback(
            arguments["selection"],
            format=arguments.get("format", "png"),
            action_id=arguments.get("actionId"),
            frame=arguments.get("frame"),
        )
    elif name == "appflow.pause":
        result = client.pause()
    elif name == "appflow.run":
        result = client.run()
    elif name == "appflow.step":
        result = client.step(arguments.get("ticks", 1))
    else:
        raise AppFlowError(f"unknown MCP tool: {name}")

    image_data = result.pop("pixelsBase64", None)
    content = [{"type": "text", "text": json.dumps(result, ensure_ascii=False, separators=(",", ":"))}]
    if image_data:
        image = base64.b64decode(image_data)
        if result.get("format") == "png":
            content.append({"type": "image", "data": base64.b64encode(image).decode("ascii"), "mimeType": "image/png"})
        else:
            content[0]["text"] = json.dumps({**result, "pixelsBase64": image_data}, ensure_ascii=False, separators=(",", ":"))
    return {"content": content}


SERVER_INFO = {"name": "vixen-appflow", "version": "1.0.0"}
MODERN_VERSION = "2026-07-28"
LEGACY_VERSIONS = {"2024-11-05", "2025-03-26", "2025-06-18"}


def response(request: dict[str, Any], client: AppFlowClient, legacy_version: str | None = None) -> tuple[dict[str, Any] | None, str | None]:
    method = request.get("method")
    request_id = request.get("id")
    params = request.get("params") or {}
    if method == "notifications/initialized" or method == "notifications/cancelled":
        return None, legacy_version
    if method == "initialize":
        requested = params.get("protocolVersion")
        version = requested if requested in LEGACY_VERSIONS else "2025-06-18"
        result = {
            "protocolVersion": version,
            "capabilities": {"tools": {"listChanged": False}},
            "serverInfo": SERVER_INFO,
        }
        legacy_version = version
    elif method == "server/discover":
        meta = params.get("_meta") or {}
        if (meta.get("io.modelcontextprotocol/protocolVersion") != MODERN_VERSION
                or not isinstance(meta.get("io.modelcontextprotocol/clientCapabilities"), dict)):
            return {"jsonrpc": "2.0", "id": request_id, "error": {
                "code": -32602, "message": "server/discover requires 2026-07-28 protocol metadata"
            }}, legacy_version
        result = {
            "resultType": "complete",
            "supportedVersions": [MODERN_VERSION],
            "capabilities": {"tools": {"listChanged": False}},
            "_meta": {"io.modelcontextprotocol/serverInfo": SERVER_INFO},
            "ttlMs": 300000,
            "cacheScope": "public",
        }
        modern = True
    elif method == "ping":
        result = {}
        modern = legacy_version is None
    elif method == "tools/list":
        result = {"tools": TOOLS}
        modern = legacy_version is None
    elif method == "tools/call":
        modern = legacy_version is None
        try:
            result = tool_content(params.get("name", ""), params.get("arguments") or {}, client)
        except (AppFlowError, KeyError, TypeError, ValueError) as exc:
            result = {"content": [{"type": "text", "text": str(exc)}], "isError": True}
    else:
        return {"jsonrpc": "2.0", "id": request_id, "error": {"code": -32601, "message": f"method not found: {method}"}}, legacy_version

    # The modern revision carries protocol identity and capabilities on each request.
    # Legacy calls follow the initialize handshake and therefore omit this metadata.
    if method not in {"initialize", "server/discover"} and legacy_version is None:
        meta = params.get("_meta") or {}
        if (meta.get("io.modelcontextprotocol/protocolVersion") != MODERN_VERSION
                or not isinstance(meta.get("io.modelcontextprotocol/clientCapabilities"), dict)):
            return {"jsonrpc": "2.0", "id": request_id, "error": {
                "code": -32602, "message": "modern MCP requests require 2026-07-28 protocol metadata"
            }}, legacy_version
        if isinstance(result, dict):
            result = {"resultType": "complete", **result,
                      "_meta": {"io.modelcontextprotocol/serverInfo": SERVER_INFO}}
    if request_id is None:
        return None, legacy_version
    return {"jsonrpc": "2.0", "id": request_id, "result": result}, legacy_version


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--descriptor", default=os.environ.get("VIXEN_APPFLOW_DESCRIPTOR"))
    parser.add_argument("--timeout", type=float, default=45.0)
    args = parser.parse_args()
    if not args.descriptor:
        parser.error("--descriptor or VIXEN_APPFLOW_DESCRIPTOR is required")
    client = AppFlowClient(args.descriptor, timeout=args.timeout)
    print(f"vixen-appflow MCP stdio connected to {args.descriptor}", file=sys.stderr, flush=True)
    legacy_version: str | None = None
    for line in sys.stdin:
        try:
            request = json.loads(line)
            if not isinstance(request, dict):
                continue
            result, legacy_version = response(request, client, legacy_version)
            if result is not None:
                sys.stdout.write(json.dumps(result, ensure_ascii=False, separators=(",", ":")) + "\n")
                sys.stdout.flush()
        except Exception as exc:  # preserve the stdio stream for the host after malformed input
            request_id = locals().get("request", {}).get("id") if isinstance(locals().get("request"), dict) else None
            reply = {"jsonrpc": "2.0", "id": request_id, "error": {"code": -32700, "message": str(exc)}}
            sys.stdout.write(json.dumps(reply, ensure_ascii=False, separators=(",", ":")) + "\n")
            sys.stdout.flush()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
