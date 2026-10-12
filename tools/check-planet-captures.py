#!/usr/bin/env python3
"""Property checks for the planet capture states (planetref P1, P5, P6, P10, P12, P14) and the contact sheet.

Reads <state>.png plus the .hits.bin / .lighting.bin / .meter.bin / .scene.json sidecars. The lit mask is
N.L > 0 of the HitRecord normal against the uploaded key light, gated by the shadow word's key bit, so cel
banding never enters the geometry checks and the fitted terminator is tested against the sim's sun direction. Decode, PNG write and the energy-bound arithmetic reuse
the lightunify checker.
"""
import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import struct

HERE = Path(__file__).resolve().parent


def load(name, file):
    spec = importlib.util.spec_from_file_location(name, HERE / file)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


lightunify = load("lightunify", "check-lightunify-captures.py")
pixels = lightunify.pixels
dot, bit = lightunify.dot, lightunify.bit

STATES = ("C1", "C1-epic", "C2", "C2-term", "C4a", "C4b", "C4c", "C5a-moon", "C7", "C7-60", "C7-60-1p5au", "C9")
SIDECARS = (".png", ".hits.bin", ".lighting.bin", ".meter.bin", ".scene.json")
RECORD = struct.Struct("<11f4I4x")
SUN_RADIUS, PLANET_RADIUS, MOON_RADIUS = 695_700_000.0, 6_371_000.0, 1_737_000.0
SUN_DIAMETER_DEGREES = 0.533
STATION_LENGTH = 100.0


class Capture:
    def __init__(self, directory, state):
        self.state = state
        self.path = directory / f"{state}.png"
        self.width, self.height, self.rgba = pixels.decode_png(self.path)
        stem = self.path.with_suffix("")
        self.scene = json.loads(stem.with_suffix(".scene.json").read_text())
        self.config = stem.with_suffix(".lighting.bin").read_bytes()
        count, self.ambient = struct.unpack_from("<If", self.config)
        self.lights = [struct.unpack_from("<3fI4f", self.config, 16 + i * 32) for i in range(count)]
        raw = stem.with_suffix(".hits.bin").read_bytes()
        if len(raw) != self.width * self.height * 64:
            raise ValueError(f"{state}: HitRecord extent does not match the capture")
        self.records = list(RECORD.iter_unpack(raw))
        self.meter = struct.unpack("<f", stem.with_suffix(".meter.bin").read_bytes())[0]
        self.exposure = struct.unpack_from("<f", self.config, self.scene["exposureOffset"])[0]
        self.bodies = {b["name"]: b for b in self.scene["bodies"]}
        # Uploaded key light: toward-the-light direction of the first lit directional light.
        self.key = next(l[:3] for l in self.lights if not l[3] and max(l[4:7]) > 0)
        self.focal = (self.height / 2) / math.tan(math.radians(self.scene["verticalFovDegrees"]) / 2)

    def body_pixels(self, *names):
        indices = {self.bodies[n]["runtimeIndex"] for n in names if n in self.bodies}
        return [(i % self.width, i // self.width, r) for i, r in enumerate(self.records)
                if r[11] & 1 and r[12] in indices]

    def off_axis_degrees(self, relative):
        forward = self.scene["forward"]
        length = math.sqrt(dot(relative, relative))
        return math.degrees(math.acos(max(-1.0, min(1.0, dot(relative, forward) / length))))


def disc_equivalent_radius(capture, pts, body_relative, radius):
    """Radius in pixels of the circle with the measured area, and the analytic value for a sphere on axis."""
    distance = math.sqrt(dot(body_relative, body_relative))
    phi = math.radians(capture.off_axis_degrees(body_relative))
    # The sphere's silhouette subtends asin(R/d) about the body direction; the image-plane area scales
    # with 1/cos^3(phi) for a small off-axis disc.
    half = math.asin(radius / distance)
    area = math.pi * (capture.focal * math.tan(half)) ** 2 / math.cos(phi) ** 3
    return math.sqrt(len(pts) / math.pi), math.sqrt(area / math.pi), math.degrees(phi)


def smallest_eigenvector(matrix):
    """Inverse power iteration on a symmetric 3x3 (Cramer's rule for the solve)."""
    a = [row[:] for row in matrix]
    trace = a[0][0] + a[1][1] + a[2][2]
    for i in range(3):
        a[i][i] += 1e-9 * max(trace, 1e-30)

    def solve(b):
        det = (a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1])
               - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0])
               + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]))
        out = []
        for col in range(3):
            m = [row[:] for row in a]
            for row in range(3):
                m[row][col] = b[row]
            out.append((m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
                        - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
                        + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0])) / det)
        return out

    v = [0.3, 0.5, 0.8]
    for _ in range(60):
        v = solve(v)
        n = math.sqrt(dot(v, v))
        v = [x / n for x in v]
    return v


def lit_pixel(capture, record):
    """Lit by the uploaded key light: N.L > 0 on the recorded normal and the shadow word's key bit set."""
    return dot(record[4:7], capture.key) > 0 and bool(record[14] & 1)


def terminator_fit(capture, body):
    """Fit the lit/unlit boundary of `body` in normal space; return (angle to the sun in degrees, boundary count, unlit fraction)."""
    pts = capture.body_pixels(body)
    if not pts:
        return None
    lit = {(x, y): lit_pixel(capture, r) for x, y, r in pts}
    unlit = sum(1 for v in lit.values() if not v)
    sun = capture.scene["sunDirection"]
    boundary = [r[4:7] for x, y, r in pts
                if any(lit.get(n) is not None and lit[n] != lit[(x, y)]
                       for n in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)))]
    if len(boundary) < 16:
        return None, len(boundary), unlit / len(pts)
    scatter = [[sum(n[i] * n[j] for n in boundary) for j in range(3)] for i in range(3)]
    axis = smallest_eigenvector(scatter)
    if dot(axis, sun) < 0:
        axis = [-x for x in axis]
    angle = math.degrees(math.acos(max(-1.0, min(1.0, dot(axis, sun)))))
    return angle, len(boundary), unlit / len(pts)


def phase_fraction(capture, body):
    """Measured lit fraction of the disc and (1 + cos alpha)/2 from the body, sun and camera positions."""
    pts = capture.body_pixels(body)
    rel = capture.bodies[body]["relative"]
    sun = capture.bodies["sun"]["relative"]
    to_sun = [s - r for s, r in zip(sun, rel)]
    to_cam = [-r for r in rel]
    cos_alpha = dot(to_sun, to_cam) / math.sqrt(dot(to_sun, to_sun) * dot(to_cam, to_cam))
    return (sum(1 for _, _, r in pts if lit_pixel(capture, r)) / len(pts) if pts else None), (1 + cos_alpha) / 2


def energy_failures(capture):
    """P10: no pixel exceeds ambient + emission + visible light, tonemapped with the meter's gain (cel bound)."""
    failures = 0
    gain = 2 ** (capture.exposure + capture.meter)
    for idx, rec in enumerate(capture.records):
        if not rec[11] & 1:
            continue
        normal, pos = rec[4:7], rec[8:11]
        emission = struct.unpack("<f", struct.pack("<I", rec[13]))[0]
        bound = capture.ambient + emission
        for li, light in enumerate(capture.lights):
            if max(light[4:7]) <= 0 or not rec[14] & bit(li):
                continue
            delta = tuple(a - b for a, b in zip(light[:3], pos)) if light[3] else light[:3]
            distance = math.sqrt(dot(delta, delta))
            if distance <= 1e-8:
                continue
            attenuation = max(0, 1 - distance / light[7]) ** 2 if light[3] and light[7] > 0 else 1
            if dot(normal, [x / distance for x in delta]) > 0:
                bound += min(1, max(light[4:7])) * attenuation
        x = max(rec[:3]) * bound * gain
        mapped = min(1, (x * (2.51 * x + .03)) / (x * (2.43 * x + .59) + .14))
        mapped = mapped * 12.92 if mapped <= .0031308 else 1.055 * mapped ** (1 / 2.4) - .055
        failures += int(max(capture.rgba[idx * 4:idx * 4 + 3]) > mapped * 255 + 4)
    return failures


def sun_disc(capture):
    """Measured and analytic sun-disc diameter in pixels, and the mean emission over the disc."""
    pts = capture.body_pixels("sun")
    if not pts:
        return None
    rel = capture.bodies["sun"]["relative"]
    distance = math.sqrt(dot(rel, rel))
    phi = math.radians(capture.off_axis_degrees(rel))
    solid_angle = 2 * math.pi * (1 - math.cos(math.asin(SUN_RADIUS / distance)))
    area = solid_angle * capture.focal ** 2 / math.cos(phi) ** 3
    emission = [struct.unpack("<f", struct.pack("<I", r[13]))[0] for _, _, r in pts]
    return dict(measuredDiameterPx=2 * math.sqrt(len(pts) / math.pi),
                analyticDiameterPx=2 * math.sqrt(area / math.pi),
                nominalAngularDiameterDegrees=math.degrees(2 * math.asin(SUN_RADIUS / distance)),
                offAxisDegrees=math.degrees(phi), meanEmission=sum(emission) / len(emission), pixels=len(pts))


def station_extent(capture):
    """Measured and analytic tip-to-tip hull extent in pixels for the chase view (spine perpendicular to the view)."""
    pts = [(x, y) for x, y, _ in capture.body_pixels("station-hull")]
    if not pts:
        return None
    rel = capture.bodies["station-hull"]["relative"]
    distance = math.sqrt(dot(rel, rel))
    candidates = {max(pts, key=k) for k in (lambda p: p[0], lambda p: -p[0], lambda p: p[1], lambda p: -p[1],
                                            lambda p: p[0] + p[1], lambda p: -p[0] - p[1],
                                            lambda p: p[0] - p[1], lambda p: p[1] - p[0])}
    a, b = max(((p, q) for p in candidates for q in candidates), key=lambda pq: math.dist(*pq))
    span = math.dist(a, b)
    # A hit pixel's centre lies inside the silhouette: add the pixel footprint along the span's direction.
    dx, dy = abs(a[0] - b[0]), abs(a[1] - b[1])
    footprint = max(dx, dy) / span if span else 1
    return dict(measuredPx=span + footprint, analyticPx=capture.focal * STATION_LENGTH / distance,
                rangeMetres=distance)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def resize_nearest(width, height, rgba, size):
    out = bytearray()
    for y in range(size):
        sy = y * height // size
        for x in range(size):
            sx = x * width // size
            out += rgba[(sy * width + sx) * 4:(sy * width + sx) * 4 + 4]
    return size, size, bytes(out)


def contact_sheet(directory, states, path, cell=360, columns=4):
    tiles = [resize_nearest(*pixels.decode_png(directory / f"{s}.png"), cell) for s in states]
    while len(tiles) % columns:
        tiles.append((cell, cell, bytes((10, 10, 10, 255)) * cell * cell))
    rgba = bytearray()
    for row in range(len(tiles) // columns):
        for y in range(cell):
            for _, _, data in tiles[row * columns:(row + 1) * columns]:
                rgba += data[y * cell * 4:(y + 1) * cell * 4]
    lightunify.write_png(path, cell * columns, cell * (len(tiles) // columns), bytes(rgba))


def tile_sheet(tiles, path, columns):
    cell = tiles[0][0]
    while len(tiles) % columns:
        tiles.append((cell, cell, bytes((10, 10, 10, 255)) * cell * cell))
    rgba = bytearray()
    for row in range(len(tiles) // columns):
        for y in range(cell):
            for _, _, data in tiles[row * columns:(row + 1) * columns]:
                rgba += data[y * cell * 4:(y + 1) * cell * 4]
    lightunify.write_png(path, cell * columns, cell * (len(tiles) // columns), bytes(rgba))


def terminator_overlay(capture, cell=360):
    """The capture with the sim's terminator (sign change of N.sun on the recorded normals) painted magenta."""
    sun = capture.scene["sunDirection"]
    index = capture.bodies[capture.scene["focus"]]["runtimeIndex"]
    w, h = capture.width, capture.height
    side = [None] * (w * h)
    for i, r in enumerate(capture.records):
        if r[11] & 1 and r[12] == index:
            side[i] = dot(r[4:7], sun) > 0
    rgba = bytearray(capture.rgba)
    reach = max(1, w // cell)
    for i, v in enumerate(side):
        if v is None:
            continue
        x, y = i % w, i // w
        if any(side[j] is not None and side[j] != v for j in ((i + 1) if x + 1 < w else i, (i + w) if y + 1 < h else i)):
            for dy in range(-reach, reach + 1):
                for dx in range(-reach, reach + 1):
                    if 0 <= x + dx < w and 0 <= y + dy < h:
                        rgba[((y + dy) * w + x + dx) * 4:((y + dy) * w + x + dx) * 4 + 4] = bytes((255, 0, 255, 255))
    return resize_nearest(w, h, bytes(rgba), cell)


def sun_detail(capture, half=12, zoom=15):
    """A (2*half)^2 crop around the sun disc at integer zoom, nearest-neighbour."""
    pts = capture.body_pixels("sun")
    cx = round(sum(p[0] for p in pts) / len(pts))
    cy = round(sum(p[1] for p in pts) / len(pts))
    size = 2 * half * zoom
    out = bytearray()
    for y in range(size):
        for x in range(size):
            sx, sy = cx - half + x // zoom, cy - half + y // zoom
            out += capture.rgba[(sy * capture.width + sx) * 4:(sy * capture.width + sx) * 4 + 4]
    return size, size, bytes(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--rerun", type=Path, help="second fresh-process run of the same states (P12)")
    parser.add_argument("--gate", action="store_true", help="exit non-zero on a failed property")
    parser.add_argument("--skip-energy", action="store_true", help="skip P10 (slow in pure Python)")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    states = [s for s in STATES if (args.directory / f"{s}.png").is_file()]
    results, failures = {}, []

    def record(state, prop, passed, **detail):
        results.setdefault(state, {})[prop] = dict(passed=passed, **detail)
        if not passed:
            failures.append(f"{state} {prop}: {detail}")

    sun_by_state = {}
    for state in states:
        c = Capture(args.directory, state)
        shading = c.scene.get("shading", "cel")

        if state in ("C1-epic", "C2-term", "C5a-moon"):
            body = "moon" if state == "C5a-moon" else "planet"
            fit = terminator_fit(c, body)
            if fit is None or fit[0] is None:
                record(state, "P1", False, reason="no terminator boundary in the frame")
            else:
                record(state, "P1", fit[0] < 1.0, angleDegrees=fit[0], boundaryPixels=fit[1], unlitFraction=fit[2])
        if state == "C1":
            fit = terminator_fit(c, "planet")
            record(state, "P1", fit is not None and fit[2] < 0.005, unlitFraction=fit[2] if fit else None,
                   note="sun behind the camera: the disc is fully lit")

        if state in ("C1-epic", "C5a-moon"):
            body = "moon" if state == "C5a-moon" else "planet"
            measured, expected = phase_fraction(c, body)
            record(state, "P5", measured is not None and abs(measured - expected) <= 0.02,
                   measuredFraction=measured, expectedFraction=expected, tolerance=0.02)

        disc = sun_disc(c)
        if disc:
            sun_by_state[state] = disc
            record(state, "P6.diameter", abs(disc["measuredDiameterPx"] - disc["analyticDiameterPx"]) <= 1.0, **disc)

        if state in ("C1", "C1-epic", "C5a-moon"):
            body = "moon" if state == "C5a-moon" else "planet"
            radius = MOON_RADIUS if body == "moon" else PLANET_RADIUS
            measured, expected, phi = disc_equivalent_radius(c, c.body_pixels(body), c.bodies[body]["relative"], radius)
            record(state, "P14", abs(measured - expected) <= 1.0, body=body, measuredRadiusPx=measured,
                   analyticRadiusPx=expected, offAxisDegrees=phi)
        if state in ("C4a", "C4b", "C4c"):
            extent = station_extent(c)
            record(state, "P14", extent is not None and abs(extent["measuredPx"] - extent["analyticPx"]) <= 1.0,
                   **(extent or {}))

        if args.skip_energy:
            pass
        elif shading == "cel":
            bad = energy_failures(c)
            record(state, "P10", bad == 0, unexplainedEnergyPixels=bad, meterEV100=c.meter)
        else:
            results.setdefault(state, {})["P10"] = dict(passed=None, note="energy bound is derived for cel shading only")

        if args.rerun:
            same = all(sha(args.directory / f"{state}{s}") == sha(args.rerun / f"{state}{s}") for s in SIDECARS)
            record(state, "P12", same)

    pair = [sun_by_state.get("C7-60"), sun_by_state.get("C7-60-1p5au")]
    if all(pair):
        ratio = pair[1]["meanEmission"] / pair[0]["meanEmission"]
        record("C7-60-1p5au", "P6.radianceInvariance", abs(ratio - 1) <= 0.01, emissionRatio=ratio,
               diameterRatio=pair[1]["measuredDiameterPx"] / pair[0]["measuredDiameterPx"], expectedDiameterRatio=1 / 1.5)

    contact_sheet(args.directory, states, args.output / "contact-sheet.png")
    overlays = [terminator_overlay(Capture(args.directory, s)) for s in ("C1-epic", "C2-term", "C5a-moon") if s in states]
    if overlays:
        tile_sheet(overlays, args.output / "terminator-overlay.png", len(overlays))
    details = [sun_detail(Capture(args.directory, s)) for s in ("C7-60", "C7-60-1p5au") if s in states]
    if details:
        tile_sheet(details, args.output / "sun-disc-detail.png", len(details))
    (args.output / "properties.json").write_text(json.dumps(dict(order=states, results=results), indent=2) + "\n")
    for state, props in results.items():
        for name, row in props.items():
            print(f"{state:12} {name:24} {'PASS' if row['passed'] else ('FAIL' if row['passed'] is False else 'N/A')}")
    print(f"contact sheet order: {', '.join(states)}")
    if failures and args.gate:
        raise SystemExit("property gates failed:\n" + "\n".join(failures))


if __name__ == "__main__":
    main()
