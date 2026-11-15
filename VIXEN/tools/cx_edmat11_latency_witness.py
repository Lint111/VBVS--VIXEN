#!/usr/bin/env python3
"""Measure editor edit-to-visible frames without the material-probe extension."""
from __future__ import annotations
import argparse, base64, json, os, subprocess, time
from pathlib import Path
from appflow_client import AppFlowClient

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--editor", type=Path, required=True); ap.add_argument("--pack", type=Path, required=True)
    ap.add_argument("--capture-dir", type=Path, required=True); ap.add_argument("--width", type=int, required=True)
    ap.add_argument("--height", type=int, required=True); a = ap.parse_args(); a.capture_dir.mkdir(parents=True, exist_ok=True)
    desc = a.capture_dir / "appflow-session.json"; desc.unlink(missing_ok=True); log = a.capture_dir / "editor.log"
    env = os.environ.copy(); env.update({"VIXEN_APPFLOW_DESCRIPTOR": str(desc), "VIXEN_EDITOR_PACK": str(a.pack),
        "VIXEN_EDITOR_PACK_SCHEMA": str(a.pack / "schemas.json"), "VIXEN_EDITOR_TEST_CAMERA": "top-down",
        "VIXEN_HIDDEN_WINDOW": "0", "VIXEN_WINDOW_WIDTH": str(a.width), "VIXEN_WINDOW_HEIGHT": str(a.height),
        "VIXEN_EXIT_AFTER_FRAMES": "10000000", "VIXEN_EDITOR_LATENCY_TRACE": "1"})
    p = None
    try:
        with log.open("w", encoding="utf-8") as out:
            p = subprocess.Popen([str(a.editor)], stdout=out, stderr=subprocess.STDOUT, env=env)
            deadline = time.monotonic() + 120
            while not desc.exists():
                if p.poll() is not None: raise RuntimeError(f"editor exited {p.returncode}")
                if time.monotonic() > deadline: raise RuntimeError("AppFlow descriptor timeout")
                time.sleep(.05)
            c = AppFlowClient(desc, timeout=240); acts = {x["name"]: x for x in c.list()["actions"]}
            events = []
            def dispatch(name, **params):
                before = log.read_text(errors="replace").count("[Compiler] Compiling stage:")
                r = c.dispatch(actionId=acts[name]["id"], params=params)
                if r["dispatchResult"]["name"] != "Ok": raise RuntimeError(f"{name}: {r}")
                shown = c.await_frame(action_id=r["actionId"]); after = log.read_text(errors="replace").count("[Compiler] Compiling stage:")
                e = {"action": name, "params": params, "frame": shown["frame"], "compiles": after-before}; events.append(e); return e
            def shot(label):
                s = c.readback({"type":"fullFrame"}, format="png"); (a.capture_dir/f"{label}.png").write_bytes(base64.b64decode(s["pixelsBase64"])); return s
            def visible(label, baseline):
                for _ in range(20):
                    s = shot(label)
                    if s["hash"] != baseline["hash"]: return {"frame":s["frame"], "hash":s["hash"], "frames":s["frame"]-baseline["frame"]}
                    c.await_frame(after_frame=s["frame"])
                raise RuntimeError(f"{label} did not change")
            c.await_frame(after_frame=5); dispatch("OpenRecipe", recipeId="hullproof:hull")
            for layer in (1,2,3): dispatch("ClearLayerMaterial", layer=layer)
            dispatch("SetParameter", parameterIndex=0, value=1.2)
            dispatch("SetLayerMaterial", layer=1, material="core:hull_metal_major")
            dispatch("SetLayerMaterial", layer=2, material="core:emissive_trim")
            base = shot("00-baseline"); change = dispatch("SetLayerMaterial", layer=2, material="core:hull_metal_minor"); mat = visible("01-material-visible", base)
            parameter_base = shot("02-parameter-before"); param = dispatch("SetParameter", parameterIndex=0, value=0.8); par = visible("03-parameter-visible", parameter_base)
            result = {"resolution":[a.width,a.height], "material": {**mat, "dispatchFrame":change["frame"], "compiles":change["compiles"]},
                      "parameter": {**par, "dispatchFrame":param["frame"], "compiles":param["compiles"]}, "events":events}
            (a.capture_dir/"witness.json").write_text(json.dumps(result, indent=2)+"\n"); c.quit(); p.wait(timeout=120); return 0
    finally:
        if p is not None and p.poll() is None: p.terminate()
if __name__ == "__main__": raise SystemExit(main())
