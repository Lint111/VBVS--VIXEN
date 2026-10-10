#!/usr/bin/env python3
"""Inspect production hit/shadow readbacks, property gates and GPU timestamps.

Uses spheres contained in authored primitives as conservative projected shadow
footprints, not a second recipe evaluator. PNG tiles retain capture orientation.
"""
import argparse
import colorsys
import csv
import importlib.util
import json
import math
from pathlib import Path
import statistics
import struct
import zlib

spec = importlib.util.spec_from_file_location("pixels", Path(__file__).with_name("compare-capture-pixels.py"))
pixels = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pixels)
STATES = ("midday", "late-afternoon", "overcast", "night-service")
GROUPS = ("machine", "rock", "foliage", "water", "figure", "ground")


def write_png(path, width, height, rgba):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    raw = b"".join(b"\0" + rgba[y*width*4:(y+1)*width*4] for y in range(height))
    path.write_bytes(pixels.PNG_SIGNATURE
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def sheet(images, path):
    width, height, _ = images[0]
    if any(image[:2] != (width, height) for image in images):
        raise ValueError("capture extents differ")
    rgba = bytearray()
    for row in range(4):
        for y in range(height):
            for _, _, data in images[row*4:(row+1)*4]:
                rgba.extend(data[y*width*4:(y+1)*width*4])
    write_png(path, width*4, height*4, rgba)


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def luma(rgb):
    return dot(rgb, (0.2126, 0.7152, 0.0722))


def bit(index):
    return 1 << (index if index < 4 else index + 12)


def inspect(path):
    width, height, rgba = pixels.decode_png(path)
    stem = path.with_suffix("")
    scene = json.loads(stem.with_suffix(".scene.json").read_text())
    config = stem.with_suffix(".lighting.bin").read_bytes()
    count, ambient = struct.unpack_from("<If", config)
    capacity = scene["lightCapacity"]
    if count > capacity:
        raise ValueError("uploaded lightCount exceeds declaration")
    lights = [struct.unpack_from("<3fI4f", config, 16+i*32) for i in range(count)]
    records = stem.with_suffix(".hits.bin").read_bytes()
    if len(records) != width*height*64:
        raise ValueError("HitRecord extent does not match capture")
    materials = {i["index"]: i["material"] for i in scene["instances"]}
    families = {g: {"tested": 0, "occluded": 0} for g in GROUPS}
    received, colours = {}, {}
    primitive_footprints = {str(i): dict(group=GROUPS[p["group"]], material=p["material"], tested=0, occluded=0) for i, p in enumerate(scene["primitives"])}
    mask = bytearray(width*height*4)
    surface_count = explained_failures = 0
    radial = [{"bins": [[] for _ in range(6)]} for _ in lights]
    exposure = struct.unpack_from("<f", config, scene["exposureOffset"])[0]
    meter_path = stem.with_suffix(".meter.bin")
    meter = struct.unpack("<f", meter_path.read_bytes())[0] if meter_path.exists() else None
    for idx in range(width*height):
        rec = struct.unpack_from("<11f4I", records, idx*64)
        if not rec[11] & 1:
            mask[idx*4:idx*4+4] = bytes((35, 35, 45, 255))
            continue
        surface_count += 1
        normal, pos = rec[4:7], rec[8:11]
        material = materials[rec[12]]
        emission = struct.unpack("<f", struct.pack("<I", rec[13]))[0]
        visibility = rec[14]
        any_shadow = False
        has_candidate = False
        energy_bound = ambient + emission
        for li, light in enumerate(lights):
            if max(light[4:7]) <= 0:
                continue
            delta = tuple(a-b for a, b in zip(light[:3], pos)) if light[3] else light[:3]
            distance = math.sqrt(dot(delta, delta))
            if distance <= 1e-8:
                continue
            direction = tuple(x/distance for x in delta)
            attenuation = max(0, 1-distance/light[7])**2 if light[3] and light[7] > 0 else 1
            if dot(normal, direction) <= 0 or attenuation == 0:
                continue
            has_candidate = True
            visible = bool(visibility & bit(li))
            any_shadow |= not visible
            if visible:
                energy_bound += min(1, max(light[4:7])) * attenuation
                if light[3]:
                    radial[li]["bins"][min(5, int(distance/light[7]*6))].append(luma(rgba[idx*4:idx*4+3]))
            # Test conservative footprints on other opaque receivers. Each ball
            # lies inside the primitive, including ellipsoids and rounded boxes.
            origin = tuple(p + n*0.02 for p, n in zip(pos, normal))
            expected = set()
            for pi, primitive in enumerate(scene["primitives"]) if idx % 4 == 0 else ():
                if primitive["material"] == material:
                    continue
                if light[3] and primitive["material"] >= 12 and math.dist(primitive["center"], light[:3]) < 1e-5:
                    continue
                center_delta = tuple(c-p for c, p in zip(primitive["center"], origin))
                along = dot(center_delta, direction)
                if along <= 0.04 or (light[3] and along >= distance):
                    continue
                radius = min(primitive["extent"])*0.65
                if dot(center_delta, center_delta)-along*along < radius*radius:
                    expected.add(GROUPS[primitive["group"]])
                    primitive_footprints[str(pi)]["tested"] += 1
                    primitive_footprints[str(pi)]["occluded"] += int(not visible)
            for group in expected:
                families[group]["tested"] += 1
                families[group]["occluded"] += int(not visible)
        # Peak-energy bound: cel bands and range attenuation cannot exceed one;
        # hue rotation preserves albedo's maximum. Account for the GPU meter,
        # rather than attributing its exposure gain to extra scene lighting.
        x = max(rec[:3]) * energy_bound * 2**(exposure+(meter or 0))
        mapped = min(1, (x*(2.51*x+.03))/(x*(2.43*x+.59)+.14))
        mapped = mapped*12.92 if mapped <= .0031308 else 1.055*mapped**(1/2.4)-.055
        explained_failures += int(max(rgba[idx*4:idx*4+3]) > mapped*255+4)
        value = 0 if any_shadow else 255
        mask[idx*4:idx*4+4] = bytes((value, value, value, 255))
        received.setdefault(str(material), [0, 0])[int(any_shadow)] += 1
        if has_candidate and normal[1] > .98 and emission == 0:
            colours.setdefault(str(material), [[], []])[int(any_shadow)].append(tuple(rgba[idx*4:idx*4+3]))
    colour_results = {}
    for material, (lit, shadow) in colours.items():
        colour = {"litSamples": len(lit), "shadowSamples": len(shadow)}
        colour_results[material] = colour
        if not lit or not shadow:
            continue
        lit_rgb = tuple(statistics.fmean(c[i] for c in lit) for i in range(3))
        shadow_rgb = tuple(statistics.fmean(c[i] for c in shadow) for i in range(3))
        hue_delta = abs(colorsys.rgb_to_hsv(*lit_rgb)[0] - colorsys.rgb_to_hsv(*shadow_rgb)[0])
        colour.update(litLuma=luma(lit_rgb), shadowLuma=luma(shadow_rgb),
            hueDeltaDegrees=min(hue_delta, 1-hue_delta)*360,
            lowerBand=luma(shadow_rgb) < luma(lit_rgb),
            receiverHue=min(hue_delta, 1-hue_delta)*360 <= 45)
    for row in radial:
        row["binCounts"] = [len(b) for b in row["bins"]]
        row["observedCombinedLuma"] = [statistics.fmean(b) if b else None for b in row.pop("bins")]
    return (width, height, rgba), (width, height, mask), dict(
        tile=path.name, lightCount=count, opaquePixels=surface_count,
        fill=surface_count/(width*height), footprintFamilies=families,
        receivingMaterials=received, receiverColours=colour_results, primitiveFootprints=primitive_footprints,
        meterEV100=meter,
        unexplainedEnergyPixels=explained_failures if meter is not None else None,
        pointLightRadialBins=radial)


def timing(directory):
    results = []
    for state in STATES:
        rows = list(csv.DictReader((directory/state/"gpu-timing.csv").open()))
        for angle in range(4):
            all_samples = [r for r in rows if angle*64 < int(r["frame"]) <= (angle+1)*64]
            samples = [r for r in all_samples if angle*64+8 < int(r["frame"])]
            for column in rows[0]:
                if not column.endswith("_ms") or column in ("cpu_frame_time_ms", "whole_frame_gpu_span_ms"):
                    continue
                values = sorted(float(r[column]) for r in samples if float(r[column]) > 0)
                results.append(dict(state=state, angle=angle, passName=column, samples=len(values),
                    p50=statistics.median(values) if values else 0,
                    p95=values[math.ceil(.95*len(values))-1] if values else 0,
                    max=max(values, default=0),
                    fullMeasuredMax=max((float(r[column]) for r in all_samples), default=0)))
    return results


def hdr_channel(channel, exposure):
    """Invert the monotone capture transform; this is measurement, not shading."""
    value = channel/255
    linear = value/12.92 if value <= .04045 else ((value+.055)/1.055)**2.4
    a, b, c = 2.43*linear-2.51, .59*linear-.03, .14*linear
    return (-b-math.sqrt(b*b-4*a*c))/(2*a) / 2**exposure


def hdr_peak(rgb, exposure):
    return hdr_channel(max(rgb), exposure)

def hdr_luma(rgb, exposure):
    return luma(tuple(hdr_channel(c, exposure) for c in rgb))

def source_falloff(directory):
    grouped = {}
    expected_sources = None
    control_energy = {}
    for angle in range(4):
        control = directory/f"source--1-angle-{angle}.png"
        _, _, checked = inspect(control)
        control_energy[str(angle)] = checked["unexplainedEnergyPixels"]
    for path in sorted(directory.glob("source-*-angle-*.png")):
        parts = path.stem.split("-angle-")
        slot = int(parts[0].removeprefix("source-"))
        if slot < 0:
            continue
        angle = int(parts[1])
        control_path = directory/f"source--1-angle-{angle}.png"
        width, height, rgb = pixels.decode_png(path)
        _, _, control = pixels.decode_png(control_path)
        stem, control_stem = path.with_suffix(""), control_path.with_suffix("")
        scene = json.loads(stem.with_suffix(".scene.json").read_text())
        materials = {i["index"]: i["material"] for i in scene["instances"]}
        config = stem.with_suffix(".lighting.bin").read_bytes()
        count = struct.unpack_from("<I", config)[0]
        sources = {i for i in range(count) if struct.unpack_from("<I", config, 16+i*32+12)[0] == 1}
        if expected_sources is None: expected_sources = sources
        if expected_sources != sources: raise ValueError("isolated source declaration changed between views")
        light = struct.unpack_from("<3fI4f", config, 16+slot*32)
        exposure = struct.unpack_from("<f", config, scene["exposureOffset"])[0]
        source_ev = exposure+struct.unpack("<f", stem.with_suffix(".meter.bin").read_bytes())[0]
        control_ev = exposure+struct.unpack("<f", control_stem.with_suffix(".meter.bin").read_bytes())[0]
        result = grouped.setdefault(str(slot), {"source": list(light[:3]), "range": light[7],
            "planes": {}, "energyFailures": 0, "measuredPixels": 0, "angles": [], "colourSamples": {}})
        result["angles"].append(angle)
        hits = stem.with_suffix(".hits.bin").read_bytes()
        for idx in range(width*height):
            rec = struct.unpack_from("<11f4I", hits, idx*64)
            # Compare a constant-colour upward planar receiver. For these warm
            # hull/earth materials hue rotation keeps red as the peak channel.
            if not rec[11] & 1 or rec[5] < .999 or materials[rec[12]] not in (0, 7):
                continue
            distance = math.dist(rec[8:11], light[:3])
            if light[1] <= rec[9] or distance >= light[7]:
                continue
            source_rgb, control_rgb = rgb[idx*4:idx*4+3], control[idx*4:idx*4+3]
            if max(source_rgb) >= 252 or max(control_rgb) >= 252:
                continue
            source_hdr = tuple(hdr_channel(c, source_ev) for c in source_rgb)
            samples = result["colourSamples"].setdefault(str(materials[rec[12]]), [[], []])
            visible = bool(rec[14] & bit(slot))
            samples[int(not visible)].append(source_hdr)
            if not visible:
                continue
            peak_contribution = max(0, hdr_peak(source_rgb, source_ev)-hdr_peak(control_rgb, control_ev))/max(rec[0], 1e-8)
            contribution = max(0, hdr_luma(source_rgb, source_ev)-hdr_luma(control_rgb, control_ev))/max(luma(rec[:3]), 1e-8)
            attenuation = (1-distance/light[7])**2
            result["measuredPixels"] += 1
            result["energyFailures"] += int(peak_contribution > min(1, max(light[4:7]))*attenuation+.015)
            plane = f"material-{materials[rec[12]]}-y-{rec[9]:.2f}"
            bins = result["planes"].setdefault(plane, [[] for _ in range(8)])
            bins[min(7, int(distance/light[7]*8))].append(contribution)
    for result in grouped.values():
        colours = {}
        for material, (lit, shadow) in result.pop("colourSamples").items():
            c = colours[material] = dict(litSamples=len(lit), shadowSamples=len(shadow), observable=min(len(lit), len(shadow)) >= 16)
            if not c["observable"]: continue
            lit_rgb = tuple(statistics.fmean(v[i] for v in lit) for i in range(3))
            shadow_rgb = tuple(statistics.fmean(v[i] for v in shadow) for i in range(3))
            h = abs(colorsys.rgb_to_hsv(*lit_rgb)[0]-colorsys.rgb_to_hsv(*shadow_rgb)[0])
            c.update(lowerBand=luma(shadow_rgb) < luma(lit_rgb), hueDeltaDegrees=min(h,1-h)*360, receiverHue=min(h,1-h)*360 <= 45)
        result["receiverColours"] = colours
        passing_planes = []
        for plane, bins in list(result["planes"].items()):
            means = [statistics.fmean(b) if len(b) >= 16 else None for b in bins]
            observed = [v for v in means if v is not None]
            tolerance = max(.002, max(observed, default=0)*.12)
            monotone = all(b <= a+tolerance for a, b in zip(observed, observed[1:]))
            decreasing = len(observed) >= 2 and observed[0] > observed[-1]+.002
            if monotone and decreasing:
                passing_planes.append(plane)
            result["planes"][plane] = dict(counts=[len(b) for b in bins],
                normalizedSourceLuminance=means, monotoneWithinTolerance=monotone,
                decreasing=decreasing, tolerance=tolerance)
        result["passingPlanes"] = passing_planes
        measured_planes = [p for p in result["planes"].values() if sum(v is not None for v in p["normalizedSourceLuminance"]) >= 2]
        result["pass"] = bool(passing_planes) and all(p["monotoneWithinTolerance"] for p in measured_planes) and result["energyFailures"] == 0 and sorted(result["angles"]) == list(range(4)) and all(c["lowerBand"] and c["receiverHue"] for c in colours.values() if c["observable"])
    if not grouped or set(map(int, grouped)) != expected_sources:
        raise ValueError("missing declared point-source captures")
    return dict(sources=grouped, ambientControlEnergyFailures=control_energy,
                passAll=all(r["pass"] for r in grouped.values()) and all(v == 0 for v in control_energy.values()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--isolation", type=Path)
    parser.add_argument("--gate", action="store_true", help="require complete passing after-change evidence")
    parser.add_argument("--before", type=Path, help="before-change generated properties/timing directory")
    args = parser.parse_args()
    if args.gate and (args.isolation is None or args.before is None):
        parser.error("--gate requires --isolation and --before")
    args.output.mkdir(parents=True, exist_ok=True)
    images, masks, properties = [], [], []
    for state in STATES:
        for angle in range(4):
            image, mask, result = inspect(args.directory/state/f"{state}-angle-{angle}.png")
            images.append(image)
            masks.append(mask)
            properties.append(result)
    sheet(images, args.output/"contact-sheet.png")
    sheet(masks, args.output/"shadow-mask-sheet.png")
    (args.output/"properties.json").write_text(json.dumps(properties, indent=2)+"\n")
    timings = timing(args.directory)
    (args.output/"gpu-timing.json").write_text(json.dumps(timings, indent=2)+"\n")
    failures = []
    if args.gate:
        for row in properties:
            for pi, footprint in row["primitiveFootprints"].items():
                footprint["coverage"] = "observable" if footprint["tested"] >= 8 else "unobservable in this projection"
                if footprint["tested"] >= 8 and footprint["occluded"] / footprint["tested"] < .95:
                    failures.append(f"{row['tile']}: primitive {pi} {footprint['group']} expected footprint occlusion {footprint['occluded']}/{footprint['tested']}")
            paired = [c for c in row["receiverColours"].values() if min(c["litSamples"], c["shadowSamples"]) >= 16]
            if row["lightCount"] <= 2 and not paired: failures.append(f"{row['tile']}: no paired receiver-colour coverage")
            if row["meterEV100"] is None or row["unexplainedEnergyPixels"] != 0:
                failures.append(f"{row['tile']}: unexplained energy or absent meter")
            for material, c in row["receiverColours"].items():
                if row["lightCount"] <= 2 and min(c["litSamples"], c["shadowSamples"]) >= 16 and (not c["lowerBand"] or not c["receiverHue"]):
                    failures.append(f"{row['tile']}: receiver colour {material}")
        if any(t["samples"] == 0 for t in timings): failures.append("missing GPU timing samples")
    if args.isolation:
        result = source_falloff(args.isolation)
        (args.output/"source-falloff.json").write_text(json.dumps(result, indent=2)+"\n")
        print(f"Point source radial gates: {sum(r['pass'] for r in result['sources'].values())}/{len(result['sources'])}")
        if args.gate and not result["passAll"]: failures.append("per-source radial falloff/ambient control")
    if args.before:
        before = json.loads((args.before/"gpu-timing.json").read_text())
        lookup = {(r["state"], r["angle"], r["passName"]): r for r in before}
        comparison = [dict(after=r, before=lookup[(r["state"], r["angle"], r["passName"])],
            p50Ratio=r["p50"]/max(lookup[(r["state"], r["angle"], r["passName"])]["p50"], 1e-9)) for r in timings]
        (args.output/"gpu-comparison.json").write_text(json.dumps(comparison, indent=2)+"\n")
    (args.output/"gate-results.json").write_text(json.dumps(dict(evaluated=args.gate, passAll=(not failures) if args.gate else None, failures=failures), indent=2)+"\n")
    print(f"Recorded {len(properties)} tiles; gray=miss, white=unoccluded, black=shadowed.")
    if failures:
        print("Property gates failed: " + "; ".join(failures))
        raise SystemExit(1)


if __name__ == "__main__":
    main()
