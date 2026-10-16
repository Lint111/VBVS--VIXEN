# planetref offline reference tooling (design `planetref.md` section 4.1 and 4.2, lane planetref-offline)

Python, outside CTest. Mitsuba 3.9.1 (BSD-3) renders the same scene description the VIXEN planet fixture
builds, so a VIXEN capture can later be compared with a physically based reference.

    ./setup_venv.sh                 # creates .venv (git-ignored) with the cp314 wheel
    ./run_reference.sh              # match blocks (offline caches) -> renders -> disc-geometry witness
    .venv/bin/python -m unittest discover -s tests
    .venv/bin/python mitsuba_export.py scenes/<id>.json   # EXR -> out/ (not committed), JSON -> regions/ (committed)

## Files

| File | Role |
|---|---|
| `scene_desc.py` | `planetref-scene/v1` loader and validator, camera model, blackbody colour |
| `mitsuba_export.py` | scene JSON -> Mitsuba scene -> EXR + `regions/<id>.regions.json` |
| `regions.py` | analytic body id map, day / terminator-band / night regions, Lambert oracle |
| `photo_match.py` | EPIC (API J2000 vectors) and ISS (Gateway record) match blocks plus scene descriptions |
| `measure_disc.py` | fits a Lambert sphere to a render; phase angle, sun-axis position angle, disc size vs the match block |
| `fixture_c1.py` | state C1 of design section 3.3 in the scene shape, as an example of what the fixture emits |
| `data/` | cached API / Gateway responses (metadata only, no images) |
| `match/`, `scenes/`, `regions/`, `witness/` | committed outputs; EXRs live in `out/` and are not committed |

## Scene description (`planetref-scene/v1`) that the fixture should emit

One JSON file per capture state. Positions are **camera-relative, metres, double** (design D2); the camera sits at
the origin. Radiance is in scene units, 1.0 = solar irradiance at 1 AU (1361 W m^-2), so a white Lambertian
surface facing the sun at 1 AU reads 1/pi.

    {
     "schema": "planetref-scene/v1", "id": "<state id>", "state": "C1", "length_unit": "m",
     "radiance_unit_w_m2": 1361.0,
     "camera": {"position_m": [0,0,0], "look_dir": [x,y,z], "up": [x,y,z], "vfov_deg": 0.6, "width": 1080, "height": 1080},
     "bodies": [
       {"name": "sun", "role": "star", "shape": "sphere", "center_m": [...], "radius_m": 6.957e8,
        "emission": {"radiance": 14719.0, "color_temperature_k": 5772.0, "limb": {"law": "none"}}},
       {"name": "planet", "role": "planet", "shape": "sphere", "center_m": [...], "radius_m": 6.371e6, "albedo": 0.30},
       {"name": "moon", "role": "moon", ... "albedo": 0.12},
       {"name": "probe", "role": "probe", "shape": "sphere", ..., "albedo": 1.0},
       {"name": "lamp0", "role": "lamp", "shape": "sphere", "center_m": [...], "radius_m": 0.1, "emission": {"power_w": 500}},
       {"name": "station", "role": "station", "shape": "box", "center_m": [...], "half_extents_m": [...], "albedo": 0.5}
     ],
     "integrator": {"max_depth": 6, "spp": 64, "seed": 1}
    }

- `role` is one of star, planet, moon, station, probe, lamp, terrain; `shape` is `sphere` or `box` (axis-aligned proxy for the hull).
- Emission is exactly one of `radiance` (scene units) or `power_w` (watts, converted through the sphere area).
- `limb`: `{"law": "none"}` or `{"law": "linear", "u": <coefficient>}` (mean-preserving, pole toward the camera).
- Planets are plain spheres: the displaced-sphere relief of the VIXEN recipe is not modelled in stage 1.
- Anything else the fixture wants to carry (match blocks, notes) goes in `note` / `match`; the exporter ignores it.

## Regions

`regions/<id>.regions.json` holds, per region, the pixel count and the mean / std / max of linear luminance plus the
mean RGB: `sky`, `<body>.disc`, and for each lit sphere `<body>.day` (solar elevation beta > 5 deg),
`<body>.terminator_band` (|beta| <= 5 deg) and `<body>.night` (beta < -5 deg). Masks come from an analytic ray cast
in double and are eroded 2 px. `analytic_sun_only_mean_luminance` is the unoccluded Lambert value from the sun alone;
a ratio far from 1 means another body shadows the region or a lamp contributes (it is an oracle, not a gate). The JSON
carries the EXR file name and SHA-256, the scene SHA-256, spp, seed and depth. Renders are deterministic: two runs
give byte-identical buffers.

## Match blocks

EPIC: `derived` holds the sun-earth-vehicle angle (angle at the Earth between the Sun and DSCOVR vectors), the
illuminated fraction k = (1 + cos SEV) / 2, the Earth disc diameter, the Moon's separation from the Earth centre, and
the position angle of the sun axis in the image (up = celestial north projected; the attitude quaternion is carried but
its convention is not used). The EPIC field of view (0.62 deg over 2048 px) is an instrument constant, not an API field.

ISS: camera = (6371 km + altitude) above the nadir point of a spherical planet; sun from elevation and azimuth at the
nadir (azimuth taken clockwise from north); look direction toward the photo centre point when the record has one, else
along the stated tilt and bearing, else **no scene is emitted** (the record does not say where the camera pointed).
Vertical FOV comes from the focal length and the sensor height in the Gateway Format line (Z9: vendor FX size).
