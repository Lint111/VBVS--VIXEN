"""Analytic body id map and region statistics for a planetref-scene/v1 description (double precision numpy)."""
import math

import numpy as np

import scene_desc as sd

TERMINATOR_BAND_DEG = 5.0
EDGE_ERODE_PX = 2


def _erode(mask, n):
    out = mask.copy()
    for _ in range(n):
        p = np.pad(out, 1, constant_values=False)
        out = p[1:-1, 1:-1] & p[:-2, 1:-1] & p[2:, 1:-1] & p[1:-1, :-2] & p[1:-1, 2:]
    return out


def _sphere_hit(origin, dirs, center, radius):
    oc = center - origin
    tca = dirs @ oc
    d2 = float(oc @ oc) - tca * tca
    inside = (tca > 0.0) & (d2 < radius * radius)
    t = np.where(inside, tca - np.sqrt(np.maximum(radius * radius - d2, 0.0)), np.inf)
    return t


def _box_hit(origin, dirs, center, half):
    lo, hi = center - half - origin, center + half - origin
    with np.errstate(divide="ignore", invalid="ignore"):
        t1, t2 = lo / dirs, hi / dirs
    tn = np.minimum(t1, t2).max(axis=-1)
    tf = np.maximum(t1, t2).min(axis=-1)
    return np.where((tf >= np.maximum(tn, 0.0)) & (tn > 0.0), tn, np.inf)


def id_map(desc):
    """Per-pixel nearest body index (-1 = sky) and hit distance."""
    cam = desc["camera"]
    origin = np.asarray(cam["position_m"], dtype=np.float64)
    dirs = sd.pixel_rays(cam)
    best_t = np.full(dirs.shape[:2], np.inf)
    best_i = np.full(dirs.shape[:2], -1, dtype=np.int32)
    for i, b in enumerate(desc["bodies"]):
        c = np.asarray(b["center_m"], dtype=np.float64)
        if b["shape"] == "sphere":
            t = _sphere_hit(origin, dirs, c, b["radius_m"])
        else:
            t = _box_hit(origin, dirs, c, np.asarray(b["half_extents_m"], dtype=np.float64))
        closer = t < best_t
        best_t = np.where(closer, t, best_t)
        best_i = np.where(closer, i, best_i)
    return best_i, best_t, dirs


def _sun_irradiance_scene(star, point):
    d = np.linalg.norm(np.asarray(star["center_m"], dtype=np.float64) - point)
    em = star["emission"]
    lum = em["radiance"] if "radiance" in em else star_radiance(star)
    return lum * math.pi * (star["radius_m"] / d) ** 2


def star_radiance(b):
    em = b["emission"]
    if "radiance" in em:
        return em["radiance"]
    area = 4.0 * math.pi * b["radius_m"] ** 2 if b["shape"] == "sphere" else None
    return em["power_w"] / area / math.pi / sd.SOLAR_IRRADIANCE_1AU_W_M2


def compute(desc, img):
    """Region statistics from a linear RGB image (H, W, 3) and the description."""
    cam = desc["camera"]
    origin = np.asarray(cam["position_m"], dtype=np.float64)
    ids, tdist, dirs = id_map(desc)
    lum = img @ np.array([0.2126, 0.7152, 0.0722])
    star = sd.star(desc)
    out = {}

    def add(name, mask, analytic=None):
        n = int(mask.sum())
        if n == 0:
            return
        e = {
            "pixels": n,
            "mean_rgb": [float(x) for x in img[mask].mean(axis=0)],
            "mean_luminance": float(lum[mask].mean()),
            "std_luminance": float(lum[mask].std()),
            "max_luminance": float(lum[mask].max()),
        }
        if analytic is not None:
            e["analytic_sun_only_mean_luminance"] = analytic
            if analytic > 0.0:
                e["ratio_render_to_analytic"] = e["mean_luminance"] / analytic
        out[name] = e

    add("sky", _erode(ids < 0, EDGE_ERODE_PX))
    for i, b in enumerate(desc["bodies"]):
        body = _erode(ids == i, EDGE_ERODE_PX)
        if "emission" in b:
            add(f"{b['name']}.disc", body)
            continue
        if b["shape"] != "sphere" or star is None:
            add(f"{b['name']}.disc", body)
            continue
        c = np.asarray(b["center_m"], dtype=np.float64)
        pts = origin + dirs * np.where(np.isfinite(tdist), tdist, 0.0)[..., None]
        n = (pts - c) / b["radius_m"]
        s = sd.unit(np.asarray(star["center_m"], dtype=np.float64) - c)
        ndl = n @ s
        beta = np.degrees(np.arcsin(np.clip(ndl, -1.0, 1.0)))
        irr = _sun_irradiance_scene(star, c)
        lam = b["albedo"] / math.pi * irr * np.maximum(ndl, 0.0)
        for label, m in (
            ("disc", body),
            ("day", body & (beta > TERMINATOR_BAND_DEG)),
            ("terminator_band", body & (np.abs(beta) <= TERMINATOR_BAND_DEG)),
            ("night", body & (beta < -TERMINATOR_BAND_DEG)),
        ):
            if m.any():
                add(f"{b['name']}.{label}", m, float(lam[m].mean()))
    return out
