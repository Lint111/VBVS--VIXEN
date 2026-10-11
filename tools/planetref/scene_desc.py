"""planetref-scene/v1: the small JSON scene description shared by the VIXEN fixture and the offline exporter.

All positions are camera-relative, in metres, in double (the per-state origin of design D2). Radiance is in
scene units where 1.0 = solar irradiance at 1 AU (1361 W m^-2), so a white Lambertian surface facing the sun
at 1 AU has radiance 1/pi.
"""
import json
import math

import numpy as np

SCHEMA = "planetref-scene/v1"
AU_M = 149_597_870_700.0
SOLAR_IRRADIANCE_1AU_W_M2 = 1361.0
SUN_RADIUS_M = 6.957e8
SHAPES = ("sphere", "box")
ROLES = ("star", "planet", "moon", "station", "probe", "lamp", "terrain")


def sun_disc_radiance_scene(radius_m=SUN_RADIUS_M):
    """Radiance of the sun's disc in scene units: E(1 AU) = L pi (R/AU)^2 with E(1 AU) = 1."""
    return (AU_M / radius_m) ** 2 / math.pi


def unit(v):
    v = np.asarray(v, dtype=np.float64)
    n = np.linalg.norm(v)
    if n == 0.0:
        raise ValueError("zero-length vector")
    return v / n


def camera_basis(cam):
    """(forward, right, up) of the image: right = forward x up, matching Mitsuba's perspective sensor."""
    fwd = unit(cam["look_dir"])
    right = unit(np.cross(fwd, np.asarray(cam["up"], dtype=np.float64)))
    up = np.cross(right, fwd)
    return fwd, right, up


def pixel_rays(cam):
    """Unit ray directions (H, W, 3) through pixel centres, row 0 at the top."""
    w, h = int(cam["width"]), int(cam["height"])
    fwd, right, up = camera_basis(cam)
    ty = math.tan(math.radians(cam["vfov_deg"]) / 2.0)
    tx = ty * w / h
    xs = (2.0 * (np.arange(w) + 0.5) / w - 1.0) * tx
    ys = (1.0 - 2.0 * (np.arange(h) + 0.5) / h) * ty
    d = fwd[None, None, :] + xs[None, :, None] * right[None, None, :] + ys[:, None, None] * up[None, None, :]
    return d / np.linalg.norm(d, axis=2, keepdims=True)


def project(cam, point_m):
    """Pixel coordinates (x, y), pixel centres at +0.5, of a camera-relative point; None if behind the camera."""
    fwd, right, up = camera_basis(cam)
    p = np.asarray(point_m, dtype=np.float64) - np.asarray(cam["position_m"], dtype=np.float64)
    z = p @ fwd
    if z <= 0.0:
        return None
    ty = math.tan(math.radians(cam["vfov_deg"]) / 2.0)
    tx = ty * cam["width"] / cam["height"]
    x = (p @ right) / z / tx
    y = (p @ up) / z / ty
    return ((x + 1.0) * 0.5 * cam["width"], (1.0 - y) * 0.5 * cam["height"])


def validate(desc):
    if desc.get("schema") != SCHEMA:
        raise ValueError(f"schema must be {SCHEMA!r}")
    for k in ("id", "camera", "bodies"):
        if k not in desc:
            raise ValueError(f"missing {k!r}")
    cam = desc["camera"]
    for k in ("position_m", "look_dir", "up", "vfov_deg", "width", "height"):
        if k not in cam:
            raise ValueError(f"camera missing {k!r}")
    names = set()
    for b in desc["bodies"]:
        if b["name"] in names:
            raise ValueError(f"duplicate body {b['name']!r}")
        names.add(b["name"])
        if b["shape"] not in SHAPES:
            raise ValueError(f"{b['name']}: unknown shape {b['shape']!r}")
        if b.get("role") not in ROLES:
            raise ValueError(f"{b['name']}: unknown role {b.get('role')!r}")
        if "center_m" not in b:
            raise ValueError(f"{b['name']}: missing center_m")
        if b["shape"] == "sphere" and "radius_m" not in b:
            raise ValueError(f"{b['name']}: sphere needs radius_m")
        if b["shape"] == "box" and "half_extents_m" not in b:
            raise ValueError(f"{b['name']}: box needs half_extents_m")
        if "emission" not in b and "albedo" not in b:
            raise ValueError(f"{b['name']}: needs albedo or emission")
        em = b.get("emission")
        if em is not None and ("radiance" in em) == ("power_w" in em):
            raise ValueError(f"{b['name']}: emission needs exactly one of radiance / power_w")
    return desc


def load(path):
    with open(path) as f:
        return validate(json.load(f))


def star(desc):
    for b in desc["bodies"]:
        if b["role"] == "star":
            return b
    return None


def planck_rgb(temperature_k):
    """Linear-sRGB colour of a blackbody normalised to luminance Y = 1 (CIE 1931 fit of Wyman et al. 2013)."""
    lam = np.arange(360.0, 831.0, 1.0)

    def g(x, mu, s1, s2):
        t = (x - mu) / np.where(x < mu, s1, s2)
        return np.exp(-0.5 * t * t)

    xbar = 1.056 * g(lam, 599.8, 37.9, 31.0) + 0.362 * g(lam, 442.0, 16.0, 26.7) - 0.065 * g(lam, 501.1, 20.4, 26.2)
    ybar = 0.821 * g(lam, 568.8, 46.9, 40.5) + 0.286 * g(lam, 530.9, 16.3, 31.1)
    zbar = 1.217 * g(lam, 437.0, 11.8, 36.0) + 0.681 * g(lam, 459.0, 26.0, 13.8)
    lm = lam * 1e-9
    h, c, k = 6.62607015e-34, 2.99792458e8, 1.380649e-23
    spec = 1.0 / (lm**5 * np.expm1(h * c / (lm * k * temperature_k)))
    xyz = np.array([(spec * xbar).sum(), (spec * ybar).sum(), (spec * zbar).sum()])
    xyz = xyz / xyz[1]
    m = np.array([[3.2404542, -1.5371385, -0.4985314], [-0.9692660, 1.8760108, 0.0415560], [0.0556434, -0.2040259, 1.0572252]])
    rgb = m @ xyz
    return rgb / (0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2])
