"""Photo-match blocks for the planetref reference catalogue (undertow reports/planetref-references.csv).

EPIC frames: viewing geometry from the DSCOVR EPIC API J2000 vectors (km). ISS frames: geometry from the
Gateway to Astronaut Photography record (nadir point, altitude, sun elevation/azimuth at nadir, focal length,
sensor). Each block is written next to a planetref-scene/v1 description of the airless reference planet seen
with that geometry. Network is used only to fill the caches under data/; --offline reads the caches.
"""
import argparse
import html
import json
import math
import os
import re
import sys
import urllib.request

import numpy as np

import scene_desc as sd

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "data")
EARTH_R_M = 6.371e6
MOON_R_M = 1.737e6
ALBEDO_PLANET = 0.30
ALBEDO_MOON = 0.12
EPIC_FOV_DEG = 0.62  # EPIC instrument constant (2048 px across 0.62 deg); not carried by the API
EPIC_PX = 2048
EPIC_API = "https://epic.gsfc.nasa.gov/api/natural/date/{date}"
GATEWAY = "https://eol.jsc.nasa.gov/SearchPhotos/photo.pl?mission={mission}&roll={roll}&frame={frame}"
SENSOR_OVERRIDE_MM = {"NIKON Z 9": (35.9, 23.9)}  # vendor FX spec, for records whose Format line has no size

EPIC_FRAMES = [
    # (scene id, capture state, API date, identifier)
    ("c1_epic_20170821T151450", "C1", "2017-08-21", "20170821151450"),
    ("c1_epic_20170821T154450", "C1", "2017-08-21", "20170821154450"),
    ("c4d_epic_20170821T181450", "C4d", "2017-08-21", "20170821181450"),
]
ISS_FRAMES = [
    ("c2_iss060_e_71360", "C2", "ISS060", "E", "71360"),
    ("c2_iss061_e_92131", "C2", "ISS061", "E", "92131"),
    ("c3_iss006_e_36913", "C3", "ISS006", "E", "36913"),
    ("c3_iss022_e_78463", "C3", "ISS022", "E", "78463"),
    ("c4_iss071_e_3390", "C4a-c", "ISS071", "E", "3390"),
    ("c6_iss049_e_49442", "C6", "ISS049", "E", "49442"),
    ("c6_iss023_e_57948", "C6", "ISS023", "E", "57948"),
]
BEARINGS = {"NORTH": 0, "NORTHEAST": 45, "EAST": 90, "SOUTHEAST": 135, "SOUTH": 180, "SOUTHWEST": 225, "WEST": 270, "NORTHWEST": 315}


def _get(url, timeout=60):
    req = urllib.request.Request(url, headers={"User-Agent": "planetref-photo-match/1"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read().decode("utf-8", errors="replace")


def _cached(path, fetch, offline):
    if os.path.exists(path):
        with open(path) as f:
            return json.load(f)
    if offline:
        raise SystemExit(f"--offline and no cache at {path}")
    data = fetch()
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        json.dump(data, f, indent=1, sort_keys=True)
        f.write("\n")
    return data


def epic_day(date, offline=False):
    return _cached(os.path.join(DATA, "epic", date + ".json"), lambda: json.loads(_get(EPIC_API.format(date=date))), offline)


def _vec(d):
    return np.array([d["x"], d["y"], d["z"]], dtype=np.float64)


def _angle_deg(a, b):
    a, b = sd.unit(a), sd.unit(b)
    return math.degrees(math.atan2(np.linalg.norm(np.cross(a, b)), float(a @ b)))


def _scene(scene_id, state, camera, bodies, match, note):
    return {
        "schema": sd.SCHEMA,
        "id": scene_id,
        "state": state,
        "length_unit": "m",
        "radiance_unit_w_m2": sd.SOLAR_IRRADIANCE_1AU_W_M2,
        "note": note,
        "camera": camera,
        "bodies": bodies,
        "integrator": {"max_depth": 6, "spp": 32, "seed": 1},
        "match": match,
    }


def _sun_body(center_m):
    return {
        "name": "sun",
        "role": "star",
        "shape": "sphere",
        "center_m": [float(x) for x in center_m],
        "radius_m": sd.SUN_RADIUS_M,
        "emission": {"radiance": sd.sun_disc_radiance_scene(), "color_temperature_k": 5772.0, "limb": {"law": "none"}},
    }


def _sphere(name, role, center_m, radius, albedo):
    return {"name": name, "role": role, "shape": "sphere", "center_m": [float(x) for x in center_m], "radius_m": radius, "albedo": albedo}


def epic_match(scene_id, state, date, identifier, offline=False, width=768):
    day = epic_day(date, offline)
    rec = next(e for e in day if e["identifier"] == identifier)
    d_km, s_km, m_km = _vec(rec["dscovr_j2000_position"]), _vec(rec["sun_j2000_position"]), _vec(rec["lunar_j2000_position"])
    dist_km = float(np.linalg.norm(d_km))
    sev = _angle_deg(s_km, d_km)
    north = np.array([0.0, 0.0, 1.0])
    look = sd.unit(-d_km)
    up = north - (north @ look) * look
    sun_dir_planet = sd.unit(s_km)
    fwd, right, upv = sd.camera_basis({"look_dir": look, "up": up})
    sun_img = (float(sun_dir_planet @ right), float(sun_dir_planet @ upv))
    derived = {
        "dscovr_earth_km": dist_km,
        "sun_earth_km": float(np.linalg.norm(s_km)),
        "sun_earth_au": float(np.linalg.norm(s_km) * 1e3 / sd.AU_M),
        "sun_earth_vehicle_deg": sev,
        "phase_fraction_k": (1.0 + math.cos(math.radians(sev))) / 2.0,
        "earth_disc_angular_diameter_deg": math.degrees(2.0 * math.asin(EARTH_R_M / 1e3 / dist_km)),
        "moon_from_earth_centre_deg": _angle_deg(m_km - d_km, -d_km),
        "moon_earth_km": float(np.linalg.norm(m_km)),
        "sun_axis_position_angle_deg": math.degrees(math.atan2(sun_img[0], sun_img[1])),
        "sun_axis_position_angle_note": "degrees clockwise from image up toward image right, of the sun direction projected on the image plane",
    }
    match = {
        "schema": "planetref-match/v1",
        "kind": "epic",
        "id": scene_id,
        "state": state,
        "identifier": identifier,
        "time_utc": rec["date"],
        "source": EPIC_API.format(date=date),
        "frame": "J2000 geocentric, km (as served by the API)",
        "vectors_km": {"dscovr": d_km.tolist(), "sun": s_km.tolist(), "moon": m_km.tolist()},
        "attitude_quaternions": rec["attitude_quaternions"],
        "centroid_coordinates": rec["centroid_coordinates"],
        "derived": derived,
        "camera": {
            "vfov_deg": EPIC_FOV_DEG,
            "fov_source": "EPIC instrument constant (0.62 deg over 2048 px); the API does not carry it",
            "look": "at the Earth centre",
            "up_source": "celestial north (J2000 +Z) projected on the image plane; the attitude quaternion convention is not used",
        },
    }
    cam = {"position_m": [0.0, 0.0, 0.0], "look_dir": look.tolist(), "up": up.tolist(), "vfov_deg": EPIC_FOV_DEG, "width": width, "height": width}
    bodies = [
        _sun_body((s_km - d_km) * 1e3),
        _sphere("earth", "planet", -d_km * 1e3, EARTH_R_M, ALBEDO_PLANET),
        _sphere("moon", "moon", (m_km - d_km) * 1e3, MOON_R_M, ALBEDO_MOON),
    ]
    note = f"airless Lambert Earth (albedo {ALBEDO_PLANET}) with the EPIC {rec['date']} UTC J2000 vectors; atmosphere and clouds omitted"
    return match, _scene(scene_id, state, cam, bodies, {"derived": derived}, note)


def _latlon(text):
    m = re.match(r"\s*([\d.]+)°\s*([NS]),\s*([\d.]+)°\s*([EW])", text or "")
    if not m:
        return None
    lat = float(m.group(1)) * (1 if m.group(2) == "N" else -1)
    lon = float(m.group(3)) * (1 if m.group(4) == "E" else -1)
    return lat, lon


def parse_gateway(page):
    t = re.sub(r"<script.*?</script>|<style.*?</style>", "", page, flags=re.S)
    t = html.unescape(re.sub(r"<[^>]+>", "\n", t))
    lines = [x.strip() for x in t.splitlines() if x.strip()]

    def after(label, n=1):
        if label not in lines:
            return None
        i = lines.index(label)
        v = lines[i + 1] if i + 1 < len(lines) else None
        return v

    def field(label, stop):
        v = after(label)
        return None if v is None or v in stop else v

    labels = {"Photo center point:", "Photo center point by machine learning:", "Nadir to Photo Center:", "Spacecraft Altitude", "Body:", "Cloud Cover Percentage:", "Sun Elevation Angle:", "Sun Azimuth:", "Camera:", "Focal Length:", "Camera Tilt:", "Format:", "Film Exposure:", "Additional Information", "Features:", "Features Found Using Machine Learning:", "Spacecraft nadir point:"}
    alt = next((re.search(r"\((\d+)\s*km\)", x) for x in lines if x.startswith(": ") and "nautical" in x), None)
    fmt = field("Format:", labels) or ""
    mm = re.search(r"([\d.]+)\s*(?:mm)?\s*x\s*([\d.]+)\s*mm", fmt)
    px = re.search(r"(\d+)\s*x\s*(\d+)\s*pixel", fmt)
    sun_el = field("Sun Elevation Angle:", labels)
    sun_az = field("Sun Azimuth:", labels)
    fl = field("Focal Length:", labels)
    tilt = field("Camera Tilt:", labels)
    tm = re.search(r"(\d+)\s*degrees", tilt or "")
    return {
        "date": lines[lines.index("Date taken") + 1],
        "time_gmt": lines[lines.index("Time taken") + 1].replace(" GMT", ""),
        "nadir": _latlon(field("Spacecraft nadir point:", labels)),
        "center": _latlon(field("Photo center point:", labels)),
        "nadir_to_center": field("Nadir to Photo Center:", labels),
        "altitude_km": float(alt.group(1)) if alt else None,
        "sun_elevation_deg": float(sun_el.strip("°")) if sun_el else None,
        "sun_azimuth_deg": float(sun_az.strip("°")) if sun_az else None,
        "camera": field("Camera:", labels),
        "focal_length_mm": float(fl.replace("mm", "")) if fl and fl.replace("mm", "").strip() else None,
        "camera_tilt": tilt,
        "camera_tilt_deg": float(tm.group(1)) if tm else None,
        "sensor_mm": [float(mm.group(1)), float(mm.group(2))] if mm else None,
        "frame_px": [int(px.group(1)), int(px.group(2))] if px else None,
    }


def gateway_record(mission, roll, frame, offline=False):
    return _cached(os.path.join(DATA, "gateway", f"{mission}-{roll}-{frame}.json"), lambda: parse_gateway(_get(GATEWAY.format(mission=mission, roll=roll, frame=frame))), offline)


def _unit_latlon(lat, lon):
    p, l = math.radians(lat), math.radians(lon)
    return np.array([math.cos(p) * math.cos(l), math.cos(p) * math.sin(l), math.sin(p)])


def iss_match(scene_id, state, mission, roll, frame, offline=False, width=960):
    rec = gateway_record(mission, roll, frame, offline)
    photo = f"{mission}-{roll}-{frame}"
    if not (rec["nadir"] and rec["altitude_km"] is not None and rec["sun_elevation_deg"] is not None and rec["sun_azimuth_deg"] is not None):
        raise SystemExit(f"{photo}: Gateway record lacks nadir/altitude/sun fields")
    lat, lon = rec["nadir"]
    zen = _unit_latlon(lat, lon)
    east = np.array([-math.sin(math.radians(lon)), math.cos(math.radians(lon)), 0.0])
    north = np.cross(zen, east)
    el, az = math.radians(rec["sun_elevation_deg"]), math.radians(rec["sun_azimuth_deg"])
    sun_dir = math.cos(el) * (math.sin(az) * east + math.cos(az) * north) + math.sin(el) * zen
    cam_pos = (EARTH_R_M + rec["altitude_km"] * 1e3) * zen
    notes = ["sun azimuth taken as degrees clockwise from north at the nadir point (Gateway convention assumed)", "planet is a sphere of 6371 km; altitude added to it", "sun placed at 1 AU"]
    if rec["center"]:
        look = sd.unit(EARTH_R_M * _unit_latlon(*rec["center"]) - cam_pos)
        look_src = "photo center point"
    elif rec["camera_tilt_deg"] is not None and rec["nadir_to_center"] and rec["nadir_to_center"].upper() in BEARINGS:
        b = math.radians(BEARINGS[rec["nadir_to_center"].upper()])
        horiz = math.cos(b) * north + math.sin(b) * east
        t = math.radians(rec["camera_tilt_deg"])
        look = math.cos(t) * (-zen) + math.sin(t) * horiz
        look_src = "camera tilt toward the stated bearing"
    else:
        look = -zen
        look_src = "assumed nadir (record has no center point or tilt)"
        notes.append("look direction is NOT in the Gateway record; no scene is emitted until an owner-supplied look direction exists")
    ref_up = zen if abs(float(look @ zen)) < 0.94 else north
    up = ref_up - (ref_up @ look) * look
    roll_src = "local zenith projected on the image plane" if ref_up is zen else "north projected on the image plane (near-nadir)"
    sensor = rec["sensor_mm"]
    if sensor is None:
        for k, v in SENSOR_OVERRIDE_MM.items():
            if rec["camera"] and rec["camera"].upper().startswith(k):
                sensor = list(v)
                notes.append(f"sensor size {v} mm from the vendor FX spec; the Gateway Format line carries none")
    vfov = None
    if rec["focal_length_mm"] and sensor:
        vfov = math.degrees(2.0 * math.atan(sensor[1] / 2.0 / rec["focal_length_mm"]))
        hfov = math.degrees(2.0 * math.atan(sensor[0] / 2.0 / rec["focal_length_mm"]))
    else:
        hfov = None
        notes.append("vertical FOV unknown (focal length or sensor size missing); no scene is emitted")
    elev = math.degrees(math.asin(float(sun_dir @ zen)))
    horizon_dip = math.degrees(math.acos(EARTH_R_M / (EARTH_R_M + rec["altitude_km"] * 1e3)))
    match = {
        "schema": "planetref-match/v1",
        "kind": "iss",
        "id": scene_id,
        "state": state,
        "photo": photo,
        "source": GATEWAY.format(mission=mission, roll=roll, frame=frame),
        "time_gmt": f"{rec['date']} {rec['time_gmt']}",
        "gateway": rec,
        "derived": {
            "camera_radius_m": float(np.linalg.norm(cam_pos)),
            "sun_elevation_at_nadir_deg": elev,
            "horizon_dip_below_horizontal_deg": horizon_dip,
            "vfov_deg": vfov,
            "hfov_deg": hfov,
            "look_dir_source": look_src,
            "image_up_source": roll_src,
            "notes": notes,
        },
    }
    if vfov is None or look_src.startswith("assumed"):
        return match, None
    aspect = (rec["frame_px"][0] / rec["frame_px"][1]) if rec["frame_px"] else sensor[0] / sensor[1]
    cam = {"position_m": [0.0, 0.0, 0.0], "look_dir": look.tolist(), "up": up.tolist(), "vfov_deg": vfov, "width": width, "height": int(round(width / aspect))}
    bodies = [_sun_body(-cam_pos + sun_dir * sd.AU_M), _sphere("earth", "planet", -cam_pos, EARTH_R_M, ALBEDO_PLANET)]
    note = f"airless Lambert Earth (albedo {ALBEDO_PLANET}) seen from {photo}; atmosphere, clouds and city lights omitted; " + look_src
    return match, _scene(scene_id, state, cam, bodies, {"derived": match["derived"]}, note)


def write_json(path, obj):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        json.dump(obj, f, indent=1, sort_keys=True)
        f.write("\n")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--offline", action="store_true", help="use data/ caches only")
    ap.add_argument("--only", help="comma-separated scene ids")
    ap.add_argument("--match-dir", default=os.path.join(HERE, "match"))
    ap.add_argument("--scene-dir", default=os.path.join(HERE, "scenes"))
    a = ap.parse_args(argv)
    only = set(a.only.split(",")) if a.only else None
    jobs = [(epic_match, (sid, st, d, i)) for sid, st, d, i in EPIC_FRAMES] + [(iss_match, (sid, st, m, r, fr)) for sid, st, m, r, fr in ISS_FRAMES]
    for fn, args in jobs:
        if only and args[0] not in only:
            continue
        match, scene = fn(*args, offline=a.offline)
        write_json(os.path.join(a.match_dir, args[0] + ".match.json"), match)
        if scene is not None:
            write_json(os.path.join(a.scene_dir, args[0] + ".json"), scene)
        d = match["derived"]
        if match["kind"] == "epic":
            print(f"{args[0]}: SEV={d['sun_earth_vehicle_deg']:.3f} deg  k={d['phase_fraction_k']:.5f}  disc={d['earth_disc_angular_diameter_deg']:.4f} deg  dist={d['dscovr_earth_km']:.0f} km  sun={d['sun_earth_au']:.4f} AU  moon_sep={d['moon_from_earth_centre_deg']:.2f} deg")
        else:
            print(f"{args[0]}: sun_el={d['sun_elevation_at_nadir_deg']:.1f} deg  vfov={d['vfov_deg']}  look={d['look_dir_source']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
