#!/usr/bin/env python3
"""Side-by-side visual-lane sheets: cel capture | Lambert twin | reference, one PNG per planet state."""
import importlib.util
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("checker", HERE / "check-planet-captures.py")
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)
pixels, write_png = checker.pixels, checker.lightunify.write_png

SPACE = "iterations/space/"
SCENES = {  # state: (reference file, crop left fraction of the reference or None)
    "C1": (SPACE + "r3-08-gas-giant-intrusion-duo.png", 0.5),
    "C1-epic": (SPACE + "r3-08-gas-giant-intrusion-duo.png", 0.5),
    "C5a-moon": (SPACE + "r3-08-gas-giant-intrusion-duo.png", 0.5),
    "C2": (SPACE + "r3-04-station-wide-hero.png", None),
    "C2-term": (SPACE + "r3-04-station-wide-hero.png", None),
    "C4a": (SPACE + "r3-04-station-wide-hero.png", None),
    "C7": (SPACE + "r3-06-slingshot-hero.png", None),
    "C7-60": (SPACE + "r3-06-slingshot-hero.png", None),
    "C7-60-1p5au": (SPACE + "r3-06-slingshot-hero.png", None),
    "C4b": (SPACE + "r3-04-station-wide-hero.png", None),
    "C4c": (SPACE + "r3-04-station-wide-hero.png", None),
    "C9": (SPACE + "r3-04-station-wide-hero.png", None),
}


def fit_height(width, height, rgba, size, crop=None):
    if crop:
        keep = int(width * crop)
        rgba = b"".join(rgba[y * width * 4:(y * width + keep) * 4] for y in range(height))
        width = keep
    out_w = max(1, width * size // height)
    out = bytearray()
    for y in range(size):
        sy = y * height // size
        for x in range(out_w):
            sx = x * width // out_w
            out += rgba[(sy * width + sx) * 4:(sy * width + sx) * 4 + 4]
    return out_w, size, bytes(out)


def main():
    cel, lambert, refs, out = (Path(a) for a in sys.argv[1:5])
    out.mkdir(parents=True, exist_ok=True)
    size = 480
    for state, (ref, crop) in SCENES.items():
        tiles = [fit_height(*pixels.decode_png(cel / f"{state}.png"), size),
                 fit_height(*pixels.decode_png(lambert / f"{state}.png"), size)]
        w, h, data = pixels.decode_png(refs / ref)
        tiles.append(fit_height(w, h, data, size, crop))
        total = sum(t[0] for t in tiles)
        rgba = bytearray()
        for y in range(size):
            for tw, _, data in tiles:
                rgba += data[y * tw * 4:(y + 1) * tw * 4]
        write_png(out / f"{state}-side-by-side.png", total, size, bytes(rgba))
        print(f"{state}: {out / (state + '-side-by-side.png')}")


if __name__ == "__main__":
    main()
