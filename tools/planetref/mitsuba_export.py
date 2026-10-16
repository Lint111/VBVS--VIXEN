"""Export a planetref-scene/v1 description to Mitsuba 3, render it, and write EXR + region-stats JSON."""
import argparse
import hashlib
import json
import math
import os
import sys

import numpy as np

import regions
import scene_desc as sd

HERE = os.path.dirname(os.path.abspath(__file__))
MITSUBA_VERSION = "3.9.1"


def _limb_bitmap(mi, em, radiance, rgb):
    """Equirectangular radiance bitmap for a sun whose pole faces the camera: row v = acos(mu) / pi."""
    law = em.get("limb", {"law": "none"})
    if law["law"] != "linear":
        return None
    u = float(law["u"])
    rows = 256
    theta = (np.arange(rows) + 0.5) / rows * math.pi
    mu = np.clip(np.cos(theta), 0.0, 1.0)
    mean = 1.0 - u / 3.0
    f = np.where(np.cos(theta) > 0.0, (1.0 - u * (1.0 - mu)) / mean, 0.0)
    arr = (f[:, None, None] * (radiance * rgb)[None, None, :]).repeat(4, axis=1)
    return mi.Bitmap(arr.astype(np.float32))


def _rotation_z_to(mi, v):
    v = sd.unit(v)
    a = np.array([0.0, 0.0, 1.0])
    ax = np.cross(a, v)
    s = np.linalg.norm(ax)
    if s < 1e-12:
        return mi.ScalarTransform4f()
    ax = ax / s
    ang = math.degrees(math.atan2(s, float(a @ v)))
    return mi.ScalarTransform4f().rotate(ax.tolist(), ang)


def build_dict(mi, desc, spp, seed, max_depth):
    cam = desc["camera"]
    pos = np.asarray(cam["position_m"], dtype=np.float64)
    fwd = sd.unit(cam["look_dir"])
    scene = {
        "type": "scene",
        "integrator": {"type": "path", "max_depth": max_depth},
        "sensor": {
            "type": "perspective",
            "fov": float(cam["vfov_deg"]),
            "fov_axis": "y",
            "near_clip": 1.0,
            "far_clip": 1e13,
            "to_world": mi.ScalarTransform4f().look_at(origin=pos.tolist(), target=(pos + fwd).tolist(), up=list(map(float, cam["up"]))),
            "sampler": {"type": "independent", "sample_count": int(spp), "seed": int(seed)},
            "film": {
                "type": "hdrfilm",
                "width": int(cam["width"]),
                "height": int(cam["height"]),
                "pixel_format": "rgb",
                "component_format": "float32",
                "rfilter": {"type": "box"},
            },
        },
    }
    for b in desc["bodies"]:
        c = np.asarray(b["center_m"], dtype=np.float64)
        if b["shape"] == "sphere":
            shape = {"type": "sphere", "center": c.tolist(), "radius": float(b["radius_m"])}
        else:
            h = np.asarray(b["half_extents_m"], dtype=np.float64)
            shape = {"type": "cube", "to_world": mi.ScalarTransform4f().translate(c.tolist()).scale(h.tolist())}
        em = b.get("emission")
        if em is None:
            shape["bsdf"] = {"type": "diffuse", "reflectance": {"type": "rgb", "value": [float(b["albedo"])] * 3}}
        else:
            radiance = regions.star_radiance(b)
            temp = em.get("color_temperature_k")
            rgb = sd.planck_rgb(temp) if temp else np.ones(3)
            shape["bsdf"] = {"type": "diffuse", "reflectance": {"type": "rgb", "value": [0.0, 0.0, 0.0]}}
            bmp = _limb_bitmap(mi, em, radiance, rgb) if b["shape"] == "sphere" else None
            if bmp is not None:
                shape = {
                    "type": "sphere",
                    "radius": float(b["radius_m"]),
                    "to_world": mi.ScalarTransform4f().translate(c.tolist()) @ _rotation_z_to(mi, pos - c),
                    "bsdf": shape["bsdf"],
                }
                radiance_spec = {"type": "bitmap", "bitmap": bmp, "raw": True, "filter_type": "bilinear", "wrap_mode": "clamp"}
            else:
                radiance_spec = {"type": "rgb", "value": (radiance * rgb).tolist()}
            shape["emitter"] = {"type": "area", "radiance": radiance_spec}
        scene[b["name"]] = shape
    return scene


def render(desc, spp=None, seed=None, max_depth=None, variant="scalar_rgb"):
    import mitsuba as mi

    mi.set_variant(variant)
    mi.set_log_level(mi.LogLevel.Error)
    integ = desc.get("integrator", {})
    spp = spp or integ.get("spp", 64)
    seed = integ.get("seed", 1) if seed is None else seed
    max_depth = max_depth or integ.get("max_depth", 6)
    scene = mi.load_dict(build_dict(mi, desc, spp, seed, max_depth))
    img = mi.render(scene, spp=spp, seed=seed)
    return np.array(img, dtype=np.float32), {"spp": spp, "seed": seed, "max_depth": max_depth, "variant": variant}


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def write_exr(path, img):
    import mitsuba as mi

    mi.Bitmap(img).write(path)


def canonical_scene_hash(desc):
    return hashlib.sha256(json.dumps(desc, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def run(scene_path, out_dir, region_dir, spp=None, seed=None, max_depth=None, variant="scalar_rgb"):
    desc = sd.load(scene_path)
    img, params = render(desc, spp, seed, max_depth, variant)
    os.makedirs(out_dir, exist_ok=True)
    os.makedirs(region_dir, exist_ok=True)
    exr = os.path.join(out_dir, desc["id"] + ".exr")
    write_exr(exr, img)
    stats = {
        "schema": "planetref-regions/v1",
        "id": desc["id"],
        "state": desc.get("state"),
        "scene_sha256": canonical_scene_hash(desc),
        "exr_file": desc["id"] + ".exr",
        "exr_sha256": sha256_file(exr),
        "renderer": {"name": "mitsuba", "version": MITSUBA_VERSION, **params},
        "radiance_unit_w_m2": desc.get("radiance_unit_w_m2", sd.SOLAR_IRRADIANCE_1AU_W_M2),
        "image": {"width": desc["camera"]["width"], "height": desc["camera"]["height"], "vfov_deg": desc["camera"]["vfov_deg"]},
        "band_deg": regions.TERMINATOR_BAND_DEG,
        "regions": regions.compute(desc, img),
    }
    out_json = os.path.join(region_dir, desc["id"] + ".regions.json")
    with open(out_json, "w") as f:
        json.dump(stats, f, indent=1, sort_keys=True)
        f.write("\n")
    return desc, img, stats, exr, out_json


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("scene", nargs="+", help="planetref-scene/v1 JSON files")
    ap.add_argument("--out", default=os.path.join(HERE, "out"), help="EXR directory (git-ignored)")
    ap.add_argument("--regions", default=os.path.join(HERE, "regions"), help="region JSON directory (committed)")
    ap.add_argument("--spp", type=int)
    ap.add_argument("--seed", type=int)
    ap.add_argument("--max-depth", type=int)
    ap.add_argument("--variant", default="scalar_rgb")
    a = ap.parse_args(argv)
    for s in a.scene:
        _, _, stats, exr, js = run(s, a.out, a.regions, a.spp, a.seed, a.max_depth, a.variant)
        print(f"{stats['id']}: {exr}  sha256={stats['exr_sha256'][:16]}  {js}")
        for k, v in stats["regions"].items():
            r = v.get("ratio_render_to_analytic")
            print(f"  {k:28s} n={v['pixels']:8d} Lmean={v['mean_luminance']:.6g}" + (f"  render/analytic={r:.4f}" if r else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
