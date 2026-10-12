#!/usr/bin/env python3
"""Compose four-angle look-dev captures beside the C16 and lighting references."""
import importlib.util
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
VISUAL = Path("/home/liory/codeman-cases/undertow/reports/visual")
GAP = 8
BACKGROUND = bytes((22, 23, 25, 255))
REFERENCES = {
    "midday": "iterations/lighting/r2-05-split-midday.png",
    "late-afternoon": "iterations/lighting/r2-06-split-late-afternoon.png",
    "overcast": "iterations/lighting/r2-15-split-overcast-fill.png",
    "night-service": "iterations/lighting/r2-16-split-night-lifted-shadow.png",
}


def load_pixels():
    path = REPO / "tools" / "check-lightunify-captures.py"
    spec = importlib.util.spec_from_file_location("lookdev_sheet_pixels", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.pixels, module.write_png


def fit_height(width, height, rgba, target_height):
    target_width = max(1, width * target_height // height)
    out = bytearray()
    for y in range(target_height):
        source_y = y * height // target_height
        for x in range(target_width):
            source_x = x * width // target_width
            offset = (source_y * width + source_x) * 4
            out.extend(rgba[offset:offset + 4])
    return target_width, target_height, bytes(out)


def paste(canvas, canvas_width, x, y, width, height, rgba):
    for row in range(height):
        src = row * width * 4
        dst = ((y + row) * canvas_width + x) * 4
        canvas[dst:dst + width * 4] = rgba[src:src + width * 4]


def compose(pixels, capture_root, state):
    captures = [pixels.decode_png(capture_root / state / f"{state}-angle-{angle}.png")
                for angle in range(4)]
    capture_width, capture_height = captures[0][:2]
    if any(image[:2] != (capture_width, capture_height) for image in captures):
        raise ValueError(f"{state}: capture angle dimensions differ")

    grid_width = capture_width * 2 + GAP
    grid_height = capture_height * 2 + GAP
    refs_height = (grid_height - GAP) // 2
    c16 = fit_height(*pixels.decode_png(VISUAL / "images" / "C16.png"), refs_height)
    state_ref = fit_height(*pixels.decode_png(VISUAL / REFERENCES[state]), refs_height)
    refs_width = max(c16[0], state_ref[0])
    sheet_width = grid_width + GAP + refs_width
    sheet_height = max(grid_height, refs_height * 2 + GAP)
    canvas = bytearray(BACKGROUND * (sheet_width * sheet_height))

    positions = ((0, 0), (capture_width + GAP, 0),
                 (0, capture_height + GAP), (capture_width + GAP, capture_height + GAP))
    for image, (x, y) in zip(captures, positions):
        paste(canvas, sheet_width, x, y, capture_width, capture_height, image[2])

    ref_x = grid_width + GAP
    for index, image in enumerate((c16, state_ref)):
        x = ref_x + (refs_width - image[0]) // 2
        y = index * (refs_height + GAP)
        paste(canvas, sheet_width, x, y, image[0], image[1], image[2])
    return sheet_width, sheet_height, bytes(canvas)


def main():
    if len(sys.argv) != 3:
        raise SystemExit(f"usage: {Path(sys.argv[0]).name} <lookdev-capture-root> <output-dir>")
    capture_root, output_dir = map(Path, sys.argv[1:])
    output_dir.mkdir(parents=True, exist_ok=True)
    pixels, write_png = load_pixels()
    for state in REFERENCES:
        width, height, rgba = compose(pixels, capture_root, state)
        output = output_dir / f"{state}-contact-sheet.png"
        write_png(output, width, height, rgba)
        print(f"{state}: {output} ({width}x{height})")


if __name__ == "__main__":
    main()
