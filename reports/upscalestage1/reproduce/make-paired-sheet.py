#!/usr/bin/env python3
"""Create an SVG contact sheet from one round of paired native captures."""

import argparse
from html import escape
from pathlib import Path


CONFIGS = (
    ("linear-scale1", "Linear · R/D 1.0"),
    ("linear-scale-2-3", "Linear · R/D ≈ 2/3"),
    ("linear-scale-1-2", "Linear · R/D 1/2"),
    ("fsr1-scale1", "FSR 1 · R/D 1.0"),
    ("fsr1-scale-2-3", "FSR 1 · R/D ≈ 2/3"),
    ("fsr1-scale-1-2", "FSR 1 · R/D 1/2"),
)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path, help="paired-native output root")
    parser.add_argument("--width", type=int, required=True)
    parser.add_argument("--height", type=int, required=True)
    parser.add_argument("--round", type=int, default=1)
    args = parser.parse_args()

    extent = f"{args.width}x{args.height}"
    base = args.root / extent / f"round-{args.round}"
    native = base / "linear-scale1" / "A-before" / "captures"
    rows = [("Native · empty upscaler slot", native)]
    rows.extend(
        (label, base / config / "B-candidate" / "captures")
        for config, label in CONFIGS
    )
    for _, directory in rows:
        for angle in range(4):
            image = directory / f"midday-angle-{angle}.png"
            if not image.is_file():
                raise FileNotFoundError(image)

    output = args.root / extent / f"comparison-sheet-round-{args.round}.svg"
    output.parent.mkdir(parents=True, exist_ok=True)
    width = 1320
    row_height = 250
    top = 92
    height = top + row_height * len(rows) + 24
    cell_width = 320
    image_width = 296 if args.width > args.height else 220
    image_height = image_width * args.height / args.width
    image_x = 180
    content = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        f'<rect width="{width}" height="{height}" fill="#f4f5f7"/>',
        '<style>text{font-family:Arial,sans-serif;fill:#20242b}.title{font-size:25px;font-weight:bold}.label{font-size:15px;font-weight:bold}.sub{font-size:12px;fill:#5d6470}</style>',
        f'<text x="24" y="34" class="title">{extent} · paired native controls and upscaler modules</text>',
        f'<text x="24" y="58" class="sub">Round {args.round}; same midday scene and four yaw views. Raw panels remain linked to their PNG files.</text>',
    ]
    for row_index, (label, directory) in enumerate(rows):
        y = top + row_index * row_height
        content.append(f'<text x="24" y="{y + 28}" class="label">{escape(label)}</text>')
        for angle in range(4):
            image = directory / f"midday-angle-{angle}.png"
            href = escape(Path(image.relative_to(args.root)).as_posix(), quote=True)
            x = image_x + angle * cell_width
            image_y = y + 4
            content.append(f'<rect x="{x}" y="{image_y}" width="{image_width}" height="{image_height:.2f}" fill="#fff" stroke="#aab0ba"/>')
            content.append(f'<image href="{href}" x="{x}" y="{image_y}" width="{image_width}" height="{image_height:.2f}" preserveAspectRatio="xMidYMid meet"/>')
            content.append(f'<text x="{x + image_width / 2:.1f}" y="{y + image_height + 22:.2f}" text-anchor="middle" class="sub">yaw {angle * 90}°</text>')
    content.append("</svg>")
    output.write_text("\n".join(content) + "\n", encoding="utf-8")
    print(output)


if __name__ == "__main__":
    main()
