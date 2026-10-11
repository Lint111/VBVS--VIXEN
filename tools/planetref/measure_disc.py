"""Recover the disc geometry (phase angle = sun-planet-camera angle, sun-axis position angle, angular
diameter) from a rendered EXR by fitting a Lambert-sphere model, and compare it with a photo-match block."""
import argparse
import json
import math
import os
import sys

import numpy as np
from scipy.optimize import least_squares

import scene_desc as sd

HERE = os.path.dirname(os.path.abspath(__file__))
SS = 2  # sub-pixel samples per axis in the model


def read_exr(path):
    import mitsuba as mi

    mi.set_variant("scalar_rgb")
    return np.array(mi.Bitmap(path), dtype=np.float64)


def _model(p, xs, ys):
    cx, cy, r, amp, alpha, psi = p
    out = np.zeros(xs.shape[:2])
    for sy in range(SS):
        for sx in range(SS):
            u = (xs + (sx + 0.5) / SS - cx) / r
            v = -(ys + (sy + 0.5) / SS - cy) / r
            rr = u * u + v * v
            z = np.sqrt(np.maximum(1.0 - rr, 0.0))
            ndl = u * math.sin(alpha) * math.sin(psi) + v * math.sin(alpha) * math.cos(psi) + z * math.cos(alpha)
            out += np.where(rr < 1.0, amp * np.maximum(ndl, 0.0), 0.0)
    return out / (SS * SS)


def fit_disc(lum):
    h, w = lum.shape
    lit = lum > 0.02 * lum.max()
    ys, xs = np.nonzero(lit)
    cx0, cy0 = xs.mean() + 0.5, ys.mean() + 0.5
    r0 = max(xs.max() - xs.min(), ys.max() - ys.min()) / 2.0 + 1.0
    x0 = np.array([cx0, cy0, r0, lum.max(), math.radians(8.0), 0.0])
    x1, x2 = int(max(cx0 - 1.15 * r0, 0)), int(min(cx0 + 1.15 * r0, w))
    y1, y2 = int(max(cy0 - 1.15 * r0, 0)), int(min(cy0 + 1.15 * r0, h))
    gx, gy = np.meshgrid(np.arange(x1, x2, dtype=np.float64), np.arange(y1, y2, dtype=np.float64))
    obs = lum[y1:y2, x1:x2]
    scale = lum.max()
    best = None
    for psi0 in np.radians(np.arange(0.0, 360.0, 45.0)):
        x0[5] = psi0
        res = least_squares(lambda p: (_model(p, gx, gy) - obs).ravel() / scale, x0, x_scale=[10, 10, 10, scale, 0.1, 0.5], method="trf")
        if best is None or res.cost < best.cost:
            best = res
    cx, cy, r, amp, alpha, psi = best.x
    if alpha < 0.0:
        alpha, psi = -alpha, psi + math.pi
    return {"cx": cx, "cy": cy, "radius_px": abs(r), "amp": amp, "alpha_deg": math.degrees(alpha), "psi_deg": math.degrees(psi) % 360.0, "rms_residual_rel": math.sqrt(2.0 * best.cost / obs.size)}


def measure(scene_path, match_path, exr_path, tol_deg=1.0):
    desc = sd.load(scene_path)
    match = json.load(open(match_path))
    cam = desc["camera"]
    img = read_exr(exr_path)
    lum = img @ np.array([0.2126, 0.7152, 0.0722])
    f = fit_disc(lum)
    half_tan = math.tan(math.radians(cam["vfov_deg"]) / 2.0)
    ang_diam = math.degrees(2.0 * math.atan(f["radius_px"] * 2.0 * half_tan / cam["height"]))
    d = match["derived"]
    alpha, psi = f["alpha_deg"], f["psi_deg"]
    k = (1.0 + math.cos(math.radians(alpha))) / 2.0
    psi_exp = d["sun_axis_position_angle_deg"] % 360.0
    dpsi = (psi - psi_exp + 180.0) % 360.0 - 180.0
    out = {
        "schema": "planetref-witness/v1",
        "id": desc["id"],
        "exr_file": os.path.basename(exr_path),
        "tolerance_deg": tol_deg,
        "model": "Lambert sphere fit: cx, cy, radius, amplitude, phase angle, sun-axis position angle",
        "fit": {k_: (round(v, 6) if isinstance(v, float) else v) for k_, v in f.items()},
        "expected_from_api": {
            "sun_earth_vehicle_deg": d["sun_earth_vehicle_deg"],
            "phase_fraction_k": d["phase_fraction_k"],
            "sun_axis_position_angle_deg": psi_exp,
            "earth_disc_angular_diameter_deg": d["earth_disc_angular_diameter_deg"],
        },
        "measured_from_render": {
            "sun_earth_vehicle_deg": alpha,
            "phase_fraction_k": k,
            "sun_axis_position_angle_deg": psi,
            "earth_disc_angular_diameter_deg": ang_diam,
        },
        "errors": {
            "sun_earth_vehicle_deg": alpha - d["sun_earth_vehicle_deg"],
            "phase_fraction_k": k - d["phase_fraction_k"],
            "phase_angle_from_k_deg": math.degrees(math.acos(2.0 * k - 1.0)) - d["sun_earth_vehicle_deg"],
            "sun_axis_position_angle_deg": dpsi,
            "earth_disc_angular_diameter_relative": ang_diam / d["earth_disc_angular_diameter_deg"] - 1.0,
        },
    }
    out["pass"] = bool(abs(out["errors"]["sun_earth_vehicle_deg"]) < tol_deg and abs(dpsi) < tol_deg and abs(out["errors"]["earth_disc_angular_diameter_relative"]) < 0.01)
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("scene_id", nargs="+")
    ap.add_argument("--out-dir", default=os.path.join(HERE, "out"))
    ap.add_argument("--witness-dir", default=os.path.join(HERE, "witness"))
    ap.add_argument("--tol-deg", type=float, default=1.0)
    a = ap.parse_args(argv)
    ok = True
    os.makedirs(a.witness_dir, exist_ok=True)
    for sid in a.scene_id:
        w = measure(os.path.join(HERE, "scenes", sid + ".json"), os.path.join(HERE, "match", sid + ".match.json"), os.path.join(a.out_dir, sid + ".exr"), a.tol_deg)
        with open(os.path.join(a.witness_dir, sid + ".witness.json"), "w") as f:
            json.dump(w, f, indent=1, sort_keys=True)
            f.write("\n")
        e, m, x = w["errors"], w["measured_from_render"], w["expected_from_api"]
        print(f"{sid}: {'PASS' if w['pass'] else 'FAIL'}")
        print(f"  SEV/phase angle  API {x['sun_earth_vehicle_deg']:.3f} deg  render {m['sun_earth_vehicle_deg']:.3f} deg  err {e['sun_earth_vehicle_deg']:+.3f}")
        print(f"  illuminated k    API {x['phase_fraction_k']:.5f}      render {m['phase_fraction_k']:.5f}      err {e['phase_fraction_k']:+.5f}")
        print(f"  sun-axis PA      API {x['sun_axis_position_angle_deg']:.3f} deg  render {m['sun_axis_position_angle_deg']:.3f} deg  err {e['sun_axis_position_angle_deg']:+.3f}")
        print(f"  disc diameter    API {x['earth_disc_angular_diameter_deg']:.4f} deg render {m['earth_disc_angular_diameter_deg']:.4f} deg rel err {e['earth_disc_angular_diameter_relative']:+.4%}")
        ok &= w["pass"]
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
