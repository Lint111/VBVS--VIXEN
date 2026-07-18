#!/usr/bin/env python3
"""
shadow_ab.py -- Task 10.1 ad-hoc A/B measurement: per-body mean luminance,
shadows ON vs OFF, for a given provider (baked/virtual), plus baked/virtual
interior-body ratio. Not part of the committed parity gate (compare_parity.py
owns that) -- this is the diagnostician's reproduction tool for the specific
"floor shadow-loss %" / "interior mean" numbers Task 10.1's brief cites.

Reuses compare_parity.py's instIdx-map parser (25x25 grid, cell=20px,
0=Lwall 1=Rwall 2=back 3=floor 4=ceil 5=light 6=sph 7=box) to mask each body's
region out of the corresponding hud_capture_150.png, then reports:
  - per-body mean luminance for the ON and OFF run
  - shadow-loss % = 100 * (1 - mean_ON / mean_OFF) per body
  - if --virtual-dir is given: interior-body (floor+box+sphere, i.e. 3,6,7)
    mean(baked_ON) / mean(virtual_ON) ratio

Usage:
    python shadow_ab.py --on-dir DIR_ON --off-dir DIR_OFF [--virtual-dir DIR]
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from compare_parity import parse_cornell_diag, load_luminance  # noqa: E402

try:
    from PIL import Image
except ImportError:
    print("shadow_ab: FAIL missing Pillow", file=sys.stderr)
    sys.exit(1)

BODY_NAMES = {
    "0": "Lwall", "1": "Rwall", "2": "back", "3": "floor",
    "4": "ceil", "5": "light", "6": "sphere", "7": "box",
}
INTERIOR_CODES = {"3", "6", "7"}  # floor, sphere, box -- diagnostician's "interior" set
GRID = 25
CELL = 20  # hud_capture_150.png is 500x500


def per_body_mean_luminance(run_dir: Path) -> dict:
    run_log = run_dir / "run.log"
    hud_png = run_dir / "hud_capture_150.png"
    cd = parse_cornell_diag(run_log)
    if cd["grid_size"] != GRID:
        raise SystemExit(f"{run_dir}: unexpected grid_size {cd['grid_size']} (expected {GRID})")

    with Image.open(hud_png) as im:
        im = im.convert("RGB")
        w, h = im.size
        px = im.load()

    sums = {c: 0.0 for c in BODY_NAMES}
    counts = {c: 0 for c in BODY_NAMES}
    for gy, row in enumerate(cd["rows"]):
        for gx, code in enumerate(row):
            if code == ".":
                continue
            cx = gx * CELL + CELL // 2
            cy = gy * CELL + CELL // 2
            if cx >= w or cy >= h:
                continue
            r, g, b = px[cx, cy]
            lum = 0.299 * r + 0.587 * g + 0.114 * b
            sums[code] += lum
            counts[code] += 1

    means = {}
    for c in BODY_NAMES:
        means[c] = (sums[c] / counts[c]) if counts[c] > 0 else None
    return means, counts


def report_on_off(on_dir: Path, off_dir: Path) -> dict:
    means_on, counts_on = per_body_mean_luminance(on_dir)
    means_off, counts_off = per_body_mean_luminance(off_dir)
    print(f"[shadow_ab] ON={on_dir}  OFF={off_dir}")
    print(f"{'body':<8}{'n_on':>6}{'n_off':>6}{'mean_ON':>10}{'mean_OFF':>10}{'loss_%':>10}")
    losses = {}
    for c, name in BODY_NAMES.items():
        mon, moff = means_on[c], means_off[c]
        if mon is None or moff is None or moff == 0:
            print(f"{name:<8}{counts_on[c]:>6}{counts_off[c]:>6}{'--':>10}{'--':>10}{'--':>10}")
            continue
        loss = 100.0 * (1.0 - mon / moff)
        losses[c] = loss
        print(f"{name:<8}{counts_on[c]:>6}{counts_off[c]:>6}{mon:>10.2f}{moff:>10.2f}{loss:>10.2f}")
    return {"means_on": means_on, "means_off": means_off, "losses_pct": losses}


def interior_ratio(baked_on_dir: Path, virtual_on_dir: Path) -> float:
    means_baked, _ = per_body_mean_luminance(baked_on_dir)
    means_virtual, _ = per_body_mean_luminance(virtual_on_dir)
    ratios = []
    for c in INTERIOR_CODES:
        b, v = means_baked.get(c), means_virtual.get(c)
        if b is not None and v is not None and v != 0:
            ratios.append(b / v)
            print(f"[shadow_ab] interior body {BODY_NAMES[c]}: baked={b:.2f} virtual={v:.2f} ratio={b/v:.3f}")
    if not ratios:
        raise SystemExit("no interior-body overlap between baked and virtual runs")
    mean_ratio = sum(ratios) / len(ratios)
    print(f"[shadow_ab] interior mean baked/virtual ratio = {mean_ratio:.3f}")
    return mean_ratio


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--on-dir", type=Path, required=True, help="run dir with shadows ON (this provider)")
    ap.add_argument("--off-dir", type=Path, required=True, help="run dir with shadows OFF (same provider)")
    ap.add_argument("--virtual-dir", type=Path, default=None,
                     help="virtual-provider shadows-ON run dir, to compute baked/virtual interior ratio")
    args = ap.parse_args()

    report_on_off(args.on_dir, args.off_dir)
    if args.virtual_dir is not None:
        interior_ratio(args.on_dir, args.virtual_dir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
