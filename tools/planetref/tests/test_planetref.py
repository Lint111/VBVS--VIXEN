import json
import math
import os
import sys
import unittest

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

import mitsuba_export as me  # noqa: E402
import photo_match as pm  # noqa: E402
import regions  # noqa: E402
import scene_desc as sd  # noqa: E402


def scene(bodies, cam=None):
    c = {"position_m": [0, 0, 0], "look_dir": [1, 0, 0], "up": [0, 0, 1], "vfov_deg": 20, "width": 200, "height": 100}
    c.update(cam or {})
    return sd.validate({"schema": sd.SCHEMA, "id": "t", "camera": c, "bodies": bodies})


SUN_SIDE = {"name": "sun", "role": "star", "shape": "sphere", "center_m": [-3e10, 1.4e11, 0], "radius_m": 6.957e8, "emission": {"radiance": sd.sun_disc_radiance_scene()}}


class MatchBlocks(unittest.TestCase):
    def test_epic_sev_and_disc(self):
        m, s = pm.epic_match("c1_epic_20170821T151450", "C1", "2017-08-21", "20170821151450", offline=True)
        d = m["derived"]
        self.assertAlmostEqual(d["sun_earth_vehicle_deg"], 7.63, delta=0.01)
        self.assertAlmostEqual(d["earth_disc_angular_diameter_deg"], 0.460, delta=0.001)
        self.assertAlmostEqual(d["moon_from_earth_centre_deg"], 2.04, delta=0.01)
        self.assertAlmostEqual(d["sun_earth_au"], 1.0116, delta=0.0005)
        sd.validate(s)

    def test_iss_sun_elevation_and_fov(self):
        m, s = pm.iss_match("c2_iss061_e_92131", "C2", "ISS061", "E", "92131", offline=True)
        self.assertAlmostEqual(m["derived"]["sun_elevation_at_nadir_deg"], 6.0, delta=1e-6)
        self.assertAlmostEqual(m["derived"]["hfov_deg"], 2.0 * math.degrees(math.atan(35.9 / 2 / 500)), delta=1e-6)
        sun = next(b for b in s["bodies"] if b["role"] == "star")
        earth = next(b for b in s["bodies"] if b["role"] == "planet")
        self.assertAlmostEqual(np.linalg.norm(np.array(sun["center_m"]) - np.array(earth["center_m"])), sd.AU_M, delta=1.0)

    def test_iss_without_look_direction_emits_no_scene(self):
        m, s = pm.iss_match("c2_iss060_e_71360", "C2", "ISS060", "E", "71360", offline=True)
        self.assertIsNone(s)
        self.assertTrue(m["derived"]["look_dir_source"].startswith("assumed"))


class Geometry(unittest.TestCase):
    def test_project_inverts_pixel_rays(self):
        cam = scene([SUN_SIDE])["camera"]
        rays = sd.pixel_rays(cam)
        p = rays[30, 150] * 5000.0
        x, y = sd.project(cam, p)
        self.assertAlmostEqual(x, 150.5, places=6)
        self.assertAlmostEqual(y, 30.5, places=6)

    def test_planck_luminance_is_one(self):
        rgb = sd.planck_rgb(5772.0)
        self.assertAlmostEqual(0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2], 1.0, places=9)
        self.assertTrue(rgb[0] > rgb[2] * 0.9)

    def test_validate_rejects_bad_emission(self):
        with self.assertRaises(ValueError):
            scene([{"name": "x", "role": "lamp", "shape": "sphere", "center_m": [1, 0, 0], "radius_m": 1, "emission": {"radiance": 1, "power_w": 2}}])


class RenderChain(unittest.TestCase):
    def test_render_matches_projection_and_lambert(self):
        lamp = {"name": "lamp", "role": "lamp", "shape": "sphere", "center_m": [1000, 60, 100], "radius_m": 10, "emission": {"radiance": 1.0}}
        ball = {"name": "ball", "role": "probe", "shape": "sphere", "center_m": [1000, -150, -40], "radius_m": 10, "albedo": 0.5}
        d = scene([SUN_SIDE, lamp, ball])
        img, _ = me.render(d, spp=16)
        lum = img.sum(axis=2)
        ys, xs = np.nonzero(lum > 0.5)
        px, py = sd.project(d["camera"], lamp["center_m"])
        self.assertAlmostEqual(xs.mean() + 0.5, px, delta=2.5)
        self.assertAlmostEqual(ys.mean() + 0.5, py, delta=2.5)

    def test_lambert_planet_under_distant_sun(self):
        planet = {"name": "p", "role": "planet", "shape": "sphere", "center_m": [2e7, 0, 0], "radius_m": 6.371e6, "albedo": 0.3}
        sun = dict(SUN_SIDE, center_m=[-0.5 * sd.AU_M, 0.866 * sd.AU_M, 0.0])
        d = scene([sun, planet], {"vfov_deg": 40, "width": 128, "height": 128})
        img, _ = me.render(d, spp=16)
        st = regions.compute(d, img)
        r = st["p.disc"]["ratio_render_to_analytic"]
        self.assertAlmostEqual(r, 1.0, delta=0.03)


if __name__ == "__main__":
    unittest.main()
