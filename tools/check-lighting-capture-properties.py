#!/usr/bin/env python3
"""Check lighting properties in the beamflake editor and HUD captures.

This intentionally checks relationships between regions rather than matching
golden pixels. The HUD upper band avoids its opaque info panels; the editor
region is an unoccluded part of the slab face.
"""

from __future__ import annotations

import argparse
import statistics
import struct
import sys
import zlib
from pathlib import Path


def read_png(path: Path) -> tuple[int, int, list[list[tuple[int, ...]]]]:
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")

    width = height = bit_depth = color_type = None
    compressed = bytearray()
    offset = 8
    while offset < len(data):
        size = struct.unpack_from(">I", data, offset)[0]
        kind = data[offset + 4 : offset + 8]
        chunk = data[offset + 8 : offset + 8 + size]
        offset += size + 12
        if kind == b"IHDR":
            width, height, bit_depth, color_type, compression, filtering, interlace = struct.unpack(
                ">IIBBBBB", chunk
            )
            if compression != 0 or filtering != 0 or interlace != 0:
                raise ValueError(f"{path}: unsupported PNG encoding")
        elif kind == b"IDAT":
            compressed.extend(chunk)
        elif kind == b"IEND":
            break

    channels_by_type = {0: 1, 2: 3, 4: 2, 6: 4}
    if bit_depth != 8 or color_type not in channels_by_type:
        raise ValueError(f"{path}: expected an 8-bit grayscale, RGB, or RGBA PNG")

    channels = channels_by_type[color_type]
    row_bytes = width * channels
    raw = zlib.decompress(compressed)
    rows: list[list[tuple[int, ...]]] = []
    previous = bytearray(row_bytes)
    position = 0

    for _ in range(height):
        filter_type = raw[position]
        source = raw[position + 1 : position + 1 + row_bytes]
        position += row_bytes + 1
        row = bytearray(row_bytes)

        for index, value in enumerate(source):
            left = row[index - channels] if index >= channels else 0
            above = previous[index]
            upper_left = previous[index - channels] if index >= channels else 0

            if filter_type == 0:
                decoded = value
            elif filter_type == 1:
                decoded = (value + left) & 0xFF
            elif filter_type == 2:
                decoded = (value + above) & 0xFF
            elif filter_type == 3:
                decoded = (value + ((left + above) // 2)) & 0xFF
            elif filter_type == 4:
                prediction = left + above - upper_left
                distance_left = abs(prediction - left)
                distance_above = abs(prediction - above)
                distance_upper_left = abs(prediction - upper_left)
                if distance_left <= distance_above and distance_left <= distance_upper_left:
                    predictor = left
                elif distance_above <= distance_upper_left:
                    predictor = above
                else:
                    predictor = upper_left
                decoded = (value + predictor) & 0xFF
            else:
                raise ValueError(f"{path}: unknown PNG row filter {filter_type}")

            row[index] = decoded

        rows.append(
            [tuple(row[x : x + channels]) for x in range(0, row_bytes, channels)]
        )
        previous = row

    return width, height, rows


def luminance(pixel: tuple[int, ...]) -> float:
    if len(pixel) == 1:
        return float(pixel[0])
    red, green, blue = pixel[:3]
    return 0.2126 * red + 0.7152 * green + 0.0722 * blue


def region_mean(
    image: tuple[int, int, list[list[tuple[int, ...]]]],
    box: tuple[int, int, int, int],
    *,
    surface_threshold: float | None = None,
) -> float:
    width, height, pixels = image
    x0, y0, x1, y1 = box
    if x0 < 0 or y0 < 0 or x1 > width or y1 > height or x0 >= x1 or y0 >= y1:
        raise ValueError(f"region {box} is outside the {width}x{height} capture")

    values = [
        luminance(pixels[y][x])
        for y in range(y0, y1)
        for x in range(x0, x1)
        if surface_threshold is None or luminance(pixels[y][x]) > surface_threshold
    ]
    if not values:
        raise ValueError(f"region {box} contains no surface pixels")
    return statistics.fmean(values)


def check_properties(baseline_path: Path, editor_path: Path, hud_path: Path) -> None:
    baseline = read_png(baseline_path)
    editor = read_png(editor_path)
    hud = read_png(hud_path)

    slab_face = (150, 150, 200, 200)
    baseline_luma = region_mean(baseline, slab_face)
    lit_luma = region_mean(editor, slab_face)
    if lit_luma <= baseline_luma:
        raise AssertionError(
            f"lit editor slab is not brighter than the unlit baseline "
            f"({lit_luma:.1f} <= {baseline_luma:.1f})"
        )
    print(f"editor slab face: baseline={baseline_luma:.1f}, lit={lit_luma:.1f}")

    hud_width, hud_height, _ = hud
    if hud_width < 445 or hud_height < 229:
        raise ValueError(f"HUD capture is too small for the body regions: {hud_width}x{hud_height}")

    body_spans = {
        "left": (30, 183),
        "center": (185, 315),
        "right": (315, 445),
    }
    for name, (x0, x1) in body_spans.items():
        midpoint = (x0 + x1) // 2
        upper_band = (190, 229)
        left_mean = region_mean(
            hud,
            (x0, upper_band[0], midpoint, upper_band[1]),
            surface_threshold=15,
        )
        right_mean = region_mean(
            hud,
            (midpoint, upper_band[0], x1, upper_band[1]),
            surface_threshold=15,
        )
        contrast = abs(left_mean - right_mean)
        if contrast <= 5:
            raise AssertionError(
                f"HUD {name} body has no directional luminance contrast "
                f"({contrast:.1f} across its upper hemisphere)"
            )
        print(f"HUD {name} body: upper-hemisphere contrast={contrast:.1f}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--unlit-editor", type=Path, required=True, help="unlit editor baseline capture")
    parser.add_argument("--lit-editor", type=Path, required=True, help="editor capture after the graph fix")
    parser.add_argument("--hud", type=Path, required=True, help="HUD capture after the graph fix")
    args = parser.parse_args()

    try:
        check_properties(args.unlit_editor, args.lit_editor, args.hud)
    except (AssertionError, OSError, ValueError, zlib.error) as error:
        print(f"lighting capture property failed: {error}", file=sys.stderr)
        return 1
    print("lighting capture properties passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
