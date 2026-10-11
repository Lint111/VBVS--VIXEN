#!/usr/bin/env python3
"""Inspect production hit/shadow readbacks and the shadow / receiver-colour gates.

Uses spheres contained in authored primitives as conservative projected shadow
footprints, not a second recipe evaluator. PNG tiles retain capture orientation.
"""
import argparse
import colorsys
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
    for row in range(len(images)//4):
        for y in range(height):
            for _, _, data in images[row*4:(row+1)*4]:
                rgba.extend(data[y*width*4:(y+1)*width*4])
    write_png(path, width*4, height*4, rgba)


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def luma(rgb):
    return dot(rgb, (0.2126, 0.7152, 0.0722))


def bit(index):
    return 1 << index


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
            colours.setdefault(str(material), [[], [], []])[int(any_shadow)].append(tuple(rgba[idx*4:idx*4+3]))
            colours[str(material)][2].append(rec[:3])
    # The receiver's own albedo is the reference: shadow pixels keep its hue up to the
    # authored cel shift plus tonemap drift; near-grey outputs carry no measurable hue.
    shift = abs(struct.unpack_from("<f", config, 16+capacity*36+24)[0])
    colour_results = {}
    for material, (lit, shadow, albedos) in colours.items():
        colour = {"litSamples": len(lit), "shadowSamples": len(shadow), "allowedHueDegrees": shift+20}
        colour_results[material] = colour
        if not shadow:
            continue
        shadow_rgb = tuple(statistics.fmean(c[i] for c in shadow) for i in range(3))
        albedo = tuple(statistics.fmean(c[i] for c in albedos) for i in range(3))
        colour["shadowChroma"] = max(shadow_rgb) - min(shadow_rgb)
        colour["observableHue"] = colour["shadowChroma"] >= 8 and max(albedo) - min(albedo) > 0
        if colour["observableHue"]:
            h = abs(colorsys.rgb_to_hsv(*shadow_rgb)[0] - colorsys.rgb_to_hsv(*albedo)[0])
            colour["hueFromAlbedoDegrees"] = min(h, 1-h)*360
            colour["receiverHue"] = colour["hueFromAlbedoDegrees"] <= shift+20
        if lit:
            lit_rgb = tuple(statistics.fmean(c[i] for c in lit) for i in range(3))
            colour.update(litLuma=luma(lit_rgb), shadowLuma=luma(shadow_rgb), lowerBand=luma(shadow_rgb) < luma(lit_rgb))
    return (width, height, rgba), (width, height, mask), dict(
        tile=path.name, lightCount=count, opaquePixels=surface_count,
        fill=surface_count/(width*height), footprintFamilies=families,
        receivingMaterials=received, receiverColours=colour_results, primitiveFootprints=primitive_footprints,
        meterEV100=meter,
        unexplainedEnergyPixels=explained_failures if meter is not None else None)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--gate", action="store_true", help="fail on the shadow and receiver-colour gates")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    images, masks, properties = [], [], []
    for state in STATES:
        if not (args.directory/state).is_dir():
            continue
        for angle in range(4):
            image, mask, result = inspect(args.directory/state/f"{state}-angle-{angle}.png")
            images.append(image)
            masks.append(mask)
            properties.append(result)
    sheet(images, args.output/"contact-sheet.png")
    sheet(masks, args.output/"shadow-mask-sheet.png")
    (args.output/"properties.json").write_text(json.dumps(properties, indent=2)+"\n")
    failures = []
    if args.gate:
        # Night tiles are recorded, never gated: the lit/shadow split is "any light reaches the
        # pixel", which mixes the key with three point lights, so foliage and glass shadows are
        # not a clean lower band of the receiver's own colour.
        for row in properties:
            if row["tile"].startswith("night-service"):
                continue
            for pi, footprint in row["primitiveFootprints"].items():
                footprint["coverage"] = "observable" if footprint["tested"] >= 8 else "unobservable in this projection"
                if footprint["tested"] >= 8 and footprint["occluded"] / footprint["tested"] < .95:
                    failures.append(f"{row['tile']}: primitive {pi} {footprint['group']} expected footprint occlusion {footprint['occluded']}/{footprint['tested']}")
            if row["meterEV100"] is None or row["unexplainedEnergyPixels"] != 0:
                failures.append(f"{row['tile']}: unexplained energy or absent meter")
            for material, c in row["receiverColours"].items():
                if c["shadowSamples"] >= 16 and c.get("observableHue") and not c["receiverHue"]:
                    failures.append(f"{row['tile']}: receiver colour {material} hue {c['hueFromAlbedoDegrees']:.0f} deg from its albedo")
                if min(c["litSamples"], c["shadowSamples"]) >= 16 and not c["lowerBand"]:
                    failures.append(f"{row['tile']}: receiver colour {material} shadow is not a lower band")
    (args.output/"gate-results.json").write_text(json.dumps(dict(evaluated=args.gate, passAll=(not failures) if args.gate else None, failures=failures), indent=2)+"\n")
    print(f"Recorded {len(properties)} tiles; gray=miss, white=unoccluded, black=shadowed.")
    if failures:
        print("Property gates failed: " + "; ".join(failures))
        raise SystemExit(1)


if __name__ == "__main__":
    main()
