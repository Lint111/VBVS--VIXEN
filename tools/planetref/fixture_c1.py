"""Writes scenes/c1_fixture.json: state C1 of planetref.md section 3.3 in the planetref-scene/v1 shape the
planetfix fixture should emit (camera at the origin, everything else camera-relative, in metres, in double)."""
import json
import math
import os

import numpy as np

import photo_match as pm
import scene_desc as sd

HERE = os.path.dirname(os.path.abspath(__file__))


def c1(width=1080):
    dist = 1.586e9
    planet = np.array([dist, 0.0, 0.0])
    sun = planet - np.array([sd.AU_M, 0.0, 0.0])
    off = math.radians(2.0)
    moon = planet + 3.844e8 * np.array([-math.cos(off), math.sin(off), 0.0])
    cam = {"position_m": [0.0, 0.0, 0.0], "look_dir": [1.0, 0.0, 0.0], "up": [0.0, 0.0, 1.0], "vfov_deg": 0.6, "width": width, "height": width}
    bodies = [pm._sun_body(sun), pm._sphere("planet", "planet", planet, pm.EARTH_R_M, pm.ALBEDO_PLANET), pm._sphere("moon", "moon", moon, pm.MOON_R_M, pm.ALBEDO_MOON)]
    note = "C1 full-day L1 view: camera on the sun-planet line 1.586e6 km sunward of the planet, moon 2 deg off that line as seen from the planet"
    return pm._scene("c1_fixture", "C1", cam, bodies, {}, note)


if __name__ == "__main__":
    pm.write_json(os.path.join(HERE, "scenes", "c1_fixture.json"), sd.validate(c1()))
