#!/usr/bin/env python3
"""Exercise shared runtime-cache behavior with concurrent VIXEN processes.

The baseline mode is used before changing cache code to record the old session-marker
collision and the old raw-cache reader's response to a torn file. Gate mode is the
registered regression witness added with the cache-envelope implementation.
"""

from __future__ import annotations

import argparse
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from typing import IO


def process_state(pid: int) -> str:
    status = pathlib.Path(f"/proc/{pid}/status")
    wchan = pathlib.Path(f"/proc/{pid}/wchan")
    state = status.read_text(errors="replace") if status.exists() else "<status unavailable>\n"
    wait_channel = wchan.read_text(errors="replace").strip() if wchan.exists() else "<unavailable>"
    return f"pid={pid} wchan={wait_channel}\n{state}"


class Child:
    def __init__(self, name: str, command: list[str], env: dict[str, str], log_path: pathlib.Path):
        self.name = name
        self.log_path = log_path
        self._log: IO[str] = log_path.open("w", encoding="utf-8")
        self.process = subprocess.Popen(
            command,
            cwd=pathlib.Path(command[0]).resolve().parent,
            env=env,
            stdout=self._log,
            stderr=subprocess.STDOUT,
            text=True,
        )

    def wait(self, timeout: float) -> int:
        try:
            result = self.process.wait(timeout=timeout)
        except subprocess.TimeoutExpired as error:
            detail = process_state(self.process.pid)
            self.stop()
            raise RuntimeError(
                f"{self.name} stalled after {timeout:.0f}s; process state follows:\n{detail}\n"
                f"log: {self.log_path}"
            ) from error
        self._log.close()
        return result

    def stop(self) -> None:
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
        if not self._log.closed:
            self._log.close()

    def log_text(self) -> str:
        if not self._log.closed:
            self._log.flush()
        return self.log_path.read_text(errors="replace")


def capture_environment(base: dict[str, str], cache: pathlib.Path, output: pathlib.Path,
                        *, editor: bool, frames: int) -> dict[str, str]:
    env = base.copy()
    env.pop("DISPLAY", None)
    env.pop("WAYLAND_DISPLAY", None)
    env.update({
        "VIXEN_CACHE_DIR": str(cache),
        "VIXEN_EXIT_AFTER_FRAMES": str(frames),
        "VIXEN_TEST_CELSHADE_LAMBERT_GGX": "0" if editor else "1",
    })
    if editor:
        env.update({
            "VIXEN_EDITOR_SCRIPT": (
                "parameter_up:0@20,parameter_up:0@21,parameter_up:0@22,parameter_up:0@23,"
                "toggle:2@30,undo@60,redo@90,save@100,reopen@110,settings@120,back@130"
            ),
            "VIXEN_EDITOR_CAPTURE_FRAMES": "5,25,45,75,105,115",
            "VIXEN_EDITOR_CAPTURE_DIR": str(output),
            "VIXEN_EDITOR_OFFSCREEN_CAPTURE": "1",
            "VIXEN_EDITOR_TEST_CAMERA": "top-down",
        })
    else:
        env.update({
            "VIXEN_HUD_SCRIPT": "A@30,B@60",
            "VIXEN_HUD_CAPTURE_FRAMES": "5,45,75",
            "VIXEN_HUD_CAPTURE_DIR": str(output),
            "VIXEN_HUD_OFFSCREEN_CAPTURE": "1",
        })
    return env


def start_app(name: str, executable: pathlib.Path, cache: pathlib.Path, output: pathlib.Path,
              log: pathlib.Path, *, editor: bool, frames: int, document: pathlib.Path) -> Child:
    output.mkdir(parents=True, exist_ok=True)
    env = capture_environment(os.environ, cache, output, editor=editor, frames=frames)
    command = [str(executable)]
    if editor:
        document_copy = output / f"{name}.vxd"
        shutil.copy2(document, document_copy)
        command.append(str(document_copy))
    return Child(name, command, env, log)


def wait_for_path(path: pathlib.Path, child: Child, timeout: float) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if path.exists():
            return
        if child.process.poll() is not None:
            raise RuntimeError(f"{child.name} exited before creating {path}; log: {child.log_path}")
        time.sleep(0.1)
    raise RuntimeError(
        f"{child.name} did not create {path} within {timeout:.0f}s; "
        f"process state:\n{process_state(child.process.pid)}\nlog: {child.log_path}"
    )


def wait_for_log_text(child: Child, text: str, timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if text in child.log_text():
            return True
        if child.process.poll() is not None:
            return text in child.log_text()
        time.sleep(0.1)
    return text in child.log_text()


def files_under(path: pathlib.Path) -> list[pathlib.Path]:
    return sorted(item for item in path.rglob("*") if item.is_file()) if path.exists() else []


def baseline(args: argparse.Namespace) -> None:
    root = pathlib.Path(tempfile.mkdtemp(prefix="vixen-multiinstance-before-", dir=args.work_dir))
    cache = root / "shared-cache"
    logs = root / "logs"
    outputs = root / "captures"
    logs.mkdir()
    outputs.mkdir()
    app = pathlib.Path(args.app).resolve()
    editor = pathlib.Path(args.editor).resolve()
    document = pathlib.Path(args.document).resolve()
    if not app.is_file() or not editor.is_file() or not document.is_file():
        raise RuntimeError("app, editor, and source document must exist")

    print(f"baseline artifacts: {root}", flush=True)
    warm = start_app("warm-app", app, cache, outputs / "warm", logs / "warm.log",
                     editor=False, frames=85, document=document)
    result = warm.wait(180)
    if result != 0:
        raise RuntimeError(f"warm-app exited {result}; log: {warm.log_path}")

    device_files = files_under(cache / "devices")
    print(f"warm cache files: {len(device_files)}", flush=True)
    for path in device_files:
        print(f"  {path.relative_to(cache)} ({path.stat().st_size} bytes)", flush=True)

    first = start_app("long-app", app, cache, outputs / "long", logs / "long.log",
                      editor=False, frames=1_000_000_000, document=document)
    second: Child | None = None
    try:
        wait_for_path(cache / ".session.lock", first, 60)
        before = files_under(cache / "devices")
        second = start_app("overlapping-app", app, cache, outputs / "overlap", logs / "overlap.log",
                           editor=False, frames=85, document=document)
        saw_warning = wait_for_log_text(
            second,
            "Previous run did not exit cleanly",
            120,
        )
        after = files_under(cache / "devices")
        removed = [path.relative_to(cache) for path in before if not path.exists()]
        print(f"overlap startup warning observed: {saw_warning}", flush=True)
        print(f"device cache files before/after overlapping startup: {len(before)}/{len(after)}", flush=True)
        print(f"device cache files removed by overlapping startup: {len(removed)}", flush=True)
        for path in removed[:20]:
            print(f"  removed {path}", flush=True)
        if not saw_warning:
            raise RuntimeError(f"overlapping-app did not report the shared-session collision; log: {second.log_path}")
        if before and not removed:
            raise RuntimeError("session-lock collision did not remove the warmed device files")
        result = second.wait(180)
        if result != 0:
            raise RuntimeError(f"overlapping-app exited {result}; log: {second.log_path}")
    finally:
        first.stop()
        if second is not None and second.process.poll() is None:
            second.stop()

    corrupt_cache = root / "corrupt-cache"
    corrupt_warm = start_app("corrupt-warm-app", app, corrupt_cache, outputs / "corrupt-warm",
                             logs / "corrupt-warm.log", editor=False, frames=85, document=document)
    result = corrupt_warm.wait(180)
    if result != 0:
        raise RuntimeError(f"corrupt-warm-app exited {result}; log: {corrupt_warm.log_path}")
    candidates = list(corrupt_cache.glob("devices/*/RenderPassCacher.cache"))
    if not candidates:
        candidates = list(corrupt_cache.glob("devices/*/MeshCacher.cache"))
    if not candidates:
        raise RuntimeError(f"no RenderPassCacher.cache or MeshCacher.cache was produced under {corrupt_cache}")
    torn = candidates[0]
    with torn.open("wb"):
        pass
    print(f"injected torn raw cache: {torn.relative_to(corrupt_cache)}", flush=True)
    corrupt_reader = start_app("torn-cache-reader", app, corrupt_cache, outputs / "corrupt-reader",
                               logs / "corrupt-reader.log", editor=False, frames=85, document=document)
    result = corrupt_reader.wait(180)
    if result != 0:
        raise RuntimeError(f"torn-cache-reader exited {result}; log: {corrupt_reader.log_path}")
    log_text = corrupt_reader.log_text()
    trusted = "Deserialization complete" in log_text
    print(f"torn cache reached the unchecked RenderPassCacher reader: {trusted}", flush=True)
    print(f"reader log: {corrupt_reader.log_path}", flush=True)


ENVELOPE_HEADER = struct.Struct("<8sIIQQQQ")
ENVELOPE_MAGIC = b"VXCACHE1"


def checksum64(data: bytes) -> int:
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return value


def verify_envelope(path: pathlib.Path, expected_identity: int, expected_type: str) -> None:
    data = path.read_bytes()
    if len(data) < ENVELOPE_HEADER.size:
        raise RuntimeError(f"truncated cache envelope: {path}")
    magic, version, _reserved, identity, type_hash, payload_size, checksum = ENVELOPE_HEADER.unpack_from(data)
    payload = data[ENVELOPE_HEADER.size:]
    if magic != ENVELOPE_MAGIC or version != 1:
        raise RuntimeError(f"invalid cache envelope header: {path}")
    if identity != expected_identity or type_hash != checksum64(expected_type.encode()):
        raise RuntimeError(f"cache envelope identity/type mismatch: {path}")
    if payload_size != len(payload) or checksum64(payload) != checksum:
        raise RuntimeError(f"cache envelope length/checksum mismatch: {path}")


def verify_cache_tree(cache: pathlib.Path) -> int:
    cache_files = 0
    for path in cache.rglob("*"):
        if not path.is_file():
            continue
        name = path.name
        if any(marker in name for marker in (".tmp.", ".payload.", ".validated.")):
            raise RuntimeError(f"temporary cache file remained after process exit: {path}")
        if path.suffix == ".cache":
            if path.parent.name.startswith("Device_0x"):
                identity = int(path.parent.name.removeprefix("Device_0x"), 16)
            else:
                identity = 0
            verify_envelope(path, identity, path.stem)
            cache_files += 1
        elif path.suffix == ".spv":
            data = path.read_bytes()
            header = struct.Struct("<IIQQQ")
            if len(data) < header.size:
                raise RuntimeError(f"truncated shader cache envelope: {path}")
            magic, version, key_hash, payload_size, checksum = header.unpack_from(data)
            payload = data[header.size:]
            if (magic != 0x43565053 or version != 1 or key_hash != checksum64(path.stem.encode()) or
                    payload_size != len(payload) or checksum64(payload) != checksum or
                    len(payload) < 4 or struct.unpack_from("<I", payload)[0] != 0x07230203):
                raise RuntimeError(f"invalid shader cache envelope: {path}")
        elif path.suffix == ".bake":
            data = path.read_bytes()
            header = struct.Struct("<8sIIQQQ")
            if len(data) < header.size:
                raise RuntimeError(f"truncated bake-artifact envelope: {path}")
            magic, version, _reserved, key_hash, payload_size, checksum = header.unpack_from(data)
            payload = data[header.size:]
            if (magic != b"VXBAKE01" or version != 1 or key_hash != checksum64(path.stem.encode()) or
                    payload_size != len(payload) or checksum64(payload) != checksum):
                raise RuntimeError(f"invalid bake-artifact envelope: {path}")
        elif name == "cacher_registry.txt":
            identity = int(path.parent.name.removeprefix("Device_0x"), 16)
            verify_envelope(path, identity, "device-cacher-registry")
        elif name == "manifest.txt" and path.parent.name == "global":
            verify_envelope(path, 0, "global-cacher-registry")
        elif path.suffix == ".json" and path.parent.name == "calibration":
            import json

            value = json.loads(path.read_text(encoding="utf-8"))
            saved_checksum = value.pop("checksum", None)
            canonical = json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode()
            if value.get("version") != 2 or str(checksum64(canonical)) != saved_checksum:
                raise RuntimeError(f"invalid calibration version/checksum: {path}")
            if "gpuVendorId" not in value or "gpuDeviceId" not in value:
                raise RuntimeError(f"calibration file has no GPU identity: {path}")
    if cache_files == 0:
        raise RuntimeError(f"no device cache envelopes were written under {cache}")
    return cache_files


def run_group(specs: list[dict[str, object]], timeout: float = 300) -> list[Child]:
    children: list[Child] = []
    try:
        for spec in specs:
            children.append(start_app(
                str(spec["name"]),
                pathlib.Path(str(spec["executable"])),
                pathlib.Path(str(spec["cache"])),
                pathlib.Path(str(spec["output"])),
                pathlib.Path(str(spec["log"])),
                editor=bool(spec["editor"]),
                frames=int(spec["frames"]),
                document=pathlib.Path(str(spec["document"])),
            ))

        deadline = time.monotonic() + timeout
        completed: dict[int, int] = {}
        while len(completed) != len(children):
            for index, child in enumerate(children):
                if index not in completed and child.process.poll() is not None:
                    completed[index] = child.wait(1)
            failed = [(children[index], code) for index, code in completed.items() if code != 0]
            if failed:
                child, code = failed[0]
                raise RuntimeError(f"{child.name} exited {code}; log: {child.log_path}")
            if len(completed) == len(children):
                break
            if time.monotonic() >= deadline:
                states = "\n".join(
                    f"{child.name}: {process_state(child.process.pid)}\nlog: {child.log_path}"
                    for child in children if child.process.poll() is None
                )
                raise RuntimeError(f"concurrent VIXEN processes stalled after {timeout:.0f}s:\n{states}")
            time.sleep(0.2)
        return children
    except Exception:
        for child in children:
            child.stop()
        raise


def capture_bytes(path: pathlib.Path) -> dict[str, bytes]:
    captures = {item.relative_to(path).as_posix(): item.read_bytes()
                for item in path.rglob("*.png") if item.is_file()}
    if not captures:
        raise RuntimeError(f"no PNG captures were produced in {path}")
    return captures


def assert_captures_equal(reference: pathlib.Path, candidate: pathlib.Path) -> None:
    expected = capture_bytes(reference)
    actual = capture_bytes(candidate)
    if expected.keys() != actual.keys():
        raise RuntimeError(
            f"capture file set differs: {reference}={sorted(expected)} {candidate}={sorted(actual)}")
    differing = [name for name in expected if expected[name] != actual[name]]
    if differing:
        raise RuntimeError(f"captures differ byte-for-byte in {candidate}: {differing}")


def app_spec(name: str, executable: pathlib.Path, cache: pathlib.Path, output: pathlib.Path,
             logs: pathlib.Path, document: pathlib.Path, *, editor: bool) -> dict[str, object]:
    return {
        "name": name,
        "executable": executable,
        "cache": cache,
        "output": output,
        "log": logs / f"{name}.log",
        "document": document,
        "editor": editor,
        "frames": 140 if editor else 85,
    }


def gate(args: argparse.Namespace) -> None:
    root = pathlib.Path(tempfile.mkdtemp(prefix="vixen-multiinstance-gate-", dir=args.work_dir))
    logs = root / "logs"
    logs.mkdir()
    app = pathlib.Path(args.app).resolve()
    editor = pathlib.Path(args.editor).resolve()
    document = pathlib.Path(args.document).resolve()
    if not app.is_file() or not editor.is_file() or not document.is_file():
        raise RuntimeError("app, editor, and source document must exist")
    print(f"gate artifacts: {root}", flush=True)

    shared_cache = root / "shared-cache"
    solo_app_output = root / "captures" / "solo-app"
    run_group([app_spec("solo-app", app, shared_cache, solo_app_output, logs, document, editor=False)])

    solo_editor_output = root / "captures" / "solo-editor"
    run_group([app_spec("solo-editor", editor, shared_cache, solo_editor_output, logs, document, editor=True)])

    two_app_specs = [
        app_spec(f"parallel-app-{suffix}", app, shared_cache, root / "captures" / f"parallel-app-{suffix}",
                 logs, document, editor=False)
        for suffix in ("a", "b")
    ]
    run_group(two_app_specs)
    assert_captures_equal(solo_app_output, pathlib.Path(two_app_specs[0]["output"]))
    assert_captures_equal(solo_app_output, pathlib.Path(two_app_specs[1]["output"]))
    print("two concurrent app captures match the solo app byte-for-byte", flush=True)

    mixed_specs = [
        app_spec("mixed-app", app, shared_cache, root / "captures" / "mixed-app", logs, document, editor=False),
        app_spec("mixed-editor", editor, shared_cache, root / "captures" / "mixed-editor", logs,
                 document, editor=True),
    ]
    run_group(mixed_specs)
    assert_captures_equal(solo_app_output, pathlib.Path(mixed_specs[0]["output"]))
    assert_captures_equal(solo_editor_output, pathlib.Path(mixed_specs[1]["output"]))
    print("concurrent editor/app captures match their solo outputs byte-for-byte", flush=True)
    print(f"warm shared cache envelopes: {verify_cache_tree(shared_cache)}", flush=True)

    cold_cache = root / "cold-cache"
    cold_specs = [
        app_spec("cold-app-a", app, cold_cache, root / "captures" / "cold-app-a", logs,
                 document, editor=False),
        app_spec("cold-app-b", app, cold_cache, root / "captures" / "cold-app-b", logs,
                 document, editor=False),
        app_spec("cold-editor", editor, cold_cache, root / "captures" / "cold-editor", logs,
                 document, editor=True),
    ]
    run_group(cold_specs)
    assert_captures_equal(solo_app_output, pathlib.Path(cold_specs[0]["output"]))
    assert_captures_equal(solo_app_output, pathlib.Path(cold_specs[1]["output"]))
    assert_captures_equal(solo_editor_output, pathlib.Path(cold_specs[2]["output"]))
    print(f"cold concurrent app/editor cache envelopes: {verify_cache_tree(cold_cache)}", flush=True)

    fresh_output = root / "captures" / "fresh-after-cold"
    fresh = run_group([app_spec("fresh-after-cold", app, cold_cache, fresh_output, logs,
                                document, editor=False)])[0]
    if "[CacheCodec] Rejected" in fresh.log_text():
        raise RuntimeError(f"fresh instance rejected a completed cache; log: {fresh.log_path}")
    print("fresh instance loaded the cold-race cache without a rejection", flush=True)

    candidates = sorted(cold_cache.glob("devices/*/VoxelSceneCacher.cache"))
    if not candidates:
        candidates = sorted(cold_cache.glob("devices/*/*.cache"))
    if not candidates:
        raise RuntimeError(f"no cache file available for torn-cache injection in {cold_cache}")
    torn = candidates[0]
    torn.write_bytes(torn.read_bytes()[:7])
    corrupt_output = root / "captures" / "torn-cache-rebuild"
    corrupt = run_group([app_spec("torn-cache-rebuild", app, cold_cache, corrupt_output, logs,
                                  document, editor=False)])[0]
    if "[CacheCodec] Rejected" not in corrupt.log_text():
        raise RuntimeError(f"torn cache was not rejected before rebuilding; log: {corrupt.log_path}")
    verify_cache_tree(cold_cache)
    final_output = root / "captures" / "fresh-after-rebuild"
    final = run_group([app_spec("fresh-after-rebuild", app, cold_cache, final_output, logs,
                                document, editor=False)])[0]
    if "[CacheCodec] Rejected" in final.log_text():
        raise RuntimeError(f"rebuilt cache was not loadable by a fresh instance; log: {final.log_path}")
    print("torn cache was rejected and rebuilt; next instance loaded the replacement", flush=True)
    print(f"collision gate passed; logs and captures: {root}", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=("baseline", "gate"), required=True)
    parser.add_argument("--app", required=True)
    parser.add_argument("--editor", required=True)
    parser.add_argument("--document", required=True)
    parser.add_argument("--work-dir", required=True)
    args = parser.parse_args()
    pathlib.Path(args.work_dir).mkdir(parents=True, exist_ok=True)
    try:
        if args.mode == "baseline":
            baseline(args)
        else:
            gate(args)
    except Exception as error:
        print(f"multiinstance cache {args.mode} failed: {error}", file=sys.stderr, flush=True)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
