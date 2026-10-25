#!/usr/bin/env python3
"""Compare paired PNG captures by file bytes and decoded pixels.

Pure standard library (no Pillow or NumPy). Optional outputs: --crops writes the changed region
of each differing pair as before|after|diff, and --contact-sheet writes one before|after|diff row
per compared pair. --reference-provenance prints the capture source and rejects a reference that
predates a declared visual change in the compared source tree.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import struct
import sys
import zlib
from pathlib import Path


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


class PngError(Exception):
    pass


def paeth(left: int, above: int, upper_left: int) -> int:
    estimate = left + above - upper_left
    left_distance = abs(estimate - left)
    above_distance = abs(estimate - above)
    upper_left_distance = abs(estimate - upper_left)
    if left_distance <= above_distance and left_distance <= upper_left_distance:
        return left
    if above_distance <= upper_left_distance:
        return above
    return upper_left


def decode_png(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if not data.startswith(PNG_SIGNATURE):
        raise PngError("invalid PNG signature")

    offset = len(PNG_SIGNATURE)
    header = None
    idat = bytearray()
    transparency = None
    saw_end = False

    while offset + 12 <= len(data):
        length = struct.unpack_from(">I", data, offset)[0]
        chunk_type = data[offset + 4 : offset + 8]
        chunk_end = offset + 12 + length
        if chunk_end > len(data):
            raise PngError("truncated PNG chunk")

        payload = data[offset + 8 : offset + 8 + length]
        expected_crc = struct.unpack_from(">I", data, offset + 8 + length)[0]
        actual_crc = zlib.crc32(chunk_type)
        actual_crc = zlib.crc32(payload, actual_crc) & 0xFFFFFFFF
        if actual_crc != expected_crc:
            raise PngError(f"CRC mismatch in {chunk_type.decode('ascii', 'replace')} chunk")

        if chunk_type == b"IHDR":
            if header is not None or length != 13:
                raise PngError("invalid IHDR chunk")
            header = struct.unpack(">IIBBBBB", payload)
        elif chunk_type == b"IDAT":
            idat.extend(payload)
        elif chunk_type == b"tRNS":
            transparency = payload
        elif chunk_type == b"IEND":
            saw_end = True
            offset = chunk_end
            break
        elif chunk_type[0] & 0x20 == 0 and chunk_type not in (b"PLTE",):
            raise PngError(f"unsupported critical chunk {chunk_type.decode('ascii', 'replace')}")

        offset = chunk_end

    if not saw_end or header is None or not idat:
        raise PngError("PNG is missing IHDR, IDAT, or IEND")
    if offset != len(data):
        raise PngError("unexpected trailing data after IEND")

    width, height, bit_depth, color_type, compression, filtering, interlace = header
    if width == 0 or height == 0:
        raise PngError("zero-sized image")
    if bit_depth != 8 or color_type not in (2, 6):
        raise PngError("only 8-bit RGB and RGBA PNG captures are supported")
    if compression != 0 or filtering != 0 or interlace != 0:
        raise PngError("unsupported PNG compression, filter, or interlace method")

    channels = 3 if color_type == 2 else 4
    bytes_per_pixel = channels
    row_bytes = width * channels
    try:
        raw = zlib.decompress(idat)
    except zlib.error as error:
        raise PngError(f"invalid compressed image data: {error}") from error
    if len(raw) != height * (row_bytes + 1):
        raise PngError("decompressed data has an unexpected size")

    decoded = bytearray(height * row_bytes)
    raw_offset = 0
    for y in range(height):
        filter_type = raw[raw_offset]
        raw_offset += 1
        row_start = y * row_bytes
        previous_start = row_start - row_bytes
        if filter_type > 4:
            raise PngError(f"unsupported PNG row filter {filter_type}")

        for x in range(row_bytes):
            value = raw[raw_offset + x]
            left = decoded[row_start + x - bytes_per_pixel] if x >= bytes_per_pixel else 0
            above = decoded[previous_start + x] if y > 0 else 0
            upper_left = (
                decoded[previous_start + x - bytes_per_pixel]
                if y > 0 and x >= bytes_per_pixel
                else 0
            )

            if filter_type == 1:
                predictor = left
            elif filter_type == 2:
                predictor = above
            elif filter_type == 3:
                predictor = (left + above) // 2
            elif filter_type == 4:
                predictor = paeth(left, above, upper_left)
            else:
                predictor = 0
            decoded[row_start + x] = (value + predictor) & 0xFF

        raw_offset += row_bytes

    if color_type == 6:
        return width, height, bytes(decoded)

    transparent_rgb = None
    if transparency is not None:
        if len(transparency) != 6:
            raise PngError("invalid RGB transparency chunk")
        transparent_rgb = struct.unpack(">HHH", transparency)

    rgba = bytearray(width * height * 4)
    for pixel_index in range(width * height):
        source = pixel_index * 3
        target = pixel_index * 4
        red, green, blue = decoded[source : source + 3]
        alpha = 0 if transparent_rgb == (red, green, blue) else 255
        rgba[target : target + 4] = bytes((red, green, blue, alpha))
    return width, height, bytes(rgba)


def encode_png(width: int, height: int, rgba: bytes) -> bytes:
    if len(rgba) != width * height * 4:
        raise PngError("pixel buffer does not match the image size")

    def chunk(kind: bytes, payload: bytes) -> bytes:
        crc = zlib.crc32(payload, zlib.crc32(kind)) & 0xFFFFFFFF
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", crc)

    rows = b"".join(b"\x00" + rgba[y * width * 4 : (y + 1) * width * 4] for y in range(height))
    return (
        PNG_SIGNATURE
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(rows))
        + chunk(b"IEND", b"")
    )


Image = tuple[int, int, bytes]


def _git_commit(repository: Path, revision: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(repository), "rev-parse", "--verify", f"{revision}^{{commit}}"],
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


def _is_ancestor(repository: Path, ancestor: str, descendant: str) -> bool:
    result = subprocess.run(
        ["git", "-C", str(repository), "merge-base", "--is-ancestor", ancestor, descendant],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode not in (0, 1):
        detail = result.stderr.strip() or "git merge-base failed"
        raise RuntimeError(detail)
    return result.returncode == 0


def _provenance_value(value: object) -> str:
    if isinstance(value, dict):
        return ", ".join(f"{key}={value[key]}" for key in sorted(value))
    if isinstance(value, list):
        return "x".join(str(item) for item in value)
    return str(value)


def check_reference_provenance(
    provenance_path: Path,
    repository: Path,
    source_commit: str | None = None,
) -> bool:
    """Print reference provenance and return whether a known visual change makes it stale."""
    try:
        provenance = json.loads(provenance_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"invalid reference provenance {provenance_path}: {error}") from error
    if not isinstance(provenance, dict):
        raise ValueError(f"reference provenance must be a JSON object: {provenance_path}")

    reference_commit = provenance.get("source_commit")
    if not isinstance(reference_commit, str) or not reference_commit:
        raise ValueError(f"reference provenance has no source_commit: {provenance_path}")

    resolved_reference = _git_commit(repository, reference_commit)
    resolved_source = _git_commit(repository, source_commit or "HEAD")
    driver = _provenance_value(provenance.get("driver", "unknown"))
    resolution = _provenance_value(provenance.get("resolution", "unknown"))
    print(
        "reference provenance: "
        f"commit={resolved_reference} "
        f"captured={provenance.get('captured_at', 'unknown')} "
        f"machine={provenance.get('machine', 'unknown')} "
        f"driver={driver} "
        f"resolution={resolution}"
    )

    changes = provenance.get("known_visual_changes", [])
    if not isinstance(changes, list):
        raise ValueError(f"known_visual_changes must be a JSON array: {provenance_path}")

    stale_changes: list[tuple[str, str]] = []
    for change in changes:
        if not isinstance(change, dict):
            raise ValueError(f"known visual change must be an object: {provenance_path}")
        change_commit = change.get("commit")
        if not isinstance(change_commit, str) or not change_commit:
            raise ValueError(f"known visual change has no commit: {provenance_path}")
        resolved_change = _git_commit(repository, change_commit)
        if (
            resolved_reference != resolved_change
            and _is_ancestor(repository, resolved_reference, resolved_change)
            and _is_ancestor(repository, resolved_change, resolved_source)
        ):
            title = str(change.get("title", "known visual change"))
            stale_changes.append((resolved_change, title))

    if stale_changes:
        print(
            f"references older than {stale_changes[0][0]}: "
            f"{stale_changes[0][1]} (reference {resolved_reference})",
            file=sys.stderr,
        )
        for commit, title in stale_changes[1:]:
            print(f"known visual change after reference: {commit}: {title}", file=sys.stderr)
        return True
    return False


def crop_image(image: Image, left: int, top: int, right: int, bottom: int) -> Image:
    width, _, rgba = image
    rows = [rgba[(y * width + left) * 4 : (y * width + right + 1) * 4] for y in range(top, bottom + 1)]
    return right - left + 1, bottom - top + 1, b"".join(rows)


def diff_image(before: Image, after: Image, tolerance: int) -> Image:
    """Changed pixels in red scaled by the largest channel delta; unchanged pixels dimmed grey."""
    width, height, before_pixels = before
    after_pixels = after[2]
    out = bytearray(width * height * 4)
    for offset in range(0, len(before_pixels), 4):
        delta = max(abs(before_pixels[offset + c] - after_pixels[offset + c]) for c in range(4))
        if delta > tolerance:
            out[offset : offset + 4] = bytes((255, 255 - min(delta, 255) // 2, 0, 255))
        else:
            grey = before_pixels[offset] // 4
            out[offset : offset + 4] = bytes((grey, grey, grey, 255))
    return width, height, bytes(out)


def scale_to_height(image: Image, height: int) -> Image:
    width, source_height, rgba = image
    if source_height == height:
        return image
    out_width = max(1, width * height // source_height)
    out = bytearray()
    for y in range(height):
        source_y = y * source_height // height
        for x in range(out_width):
            source = (source_y * width + x * width // out_width) * 4
            out += rgba[source : source + 4]
    return out_width, height, bytes(out)


def stack_horizontal(tiles: list[Image], gap: int = 2) -> Image:
    height = max(tile[1] for tile in tiles)
    width = sum(tile[0] for tile in tiles) + gap * (len(tiles) - 1)
    out = bytearray(bytes((32, 32, 32, 255)) * (width * height))
    x_origin = 0
    for tile_width, tile_height, rgba in tiles:
        for y in range(tile_height):
            target = (y * width + x_origin) * 4
            out[target : target + tile_width * 4] = rgba[y * tile_width * 4 : (y + 1) * tile_width * 4]
        x_origin += tile_width + gap
    return width, height, bytes(out)


def stack_vertical(rows: list[Image], gap: int = 4) -> Image:
    width = max(row[0] for row in rows)
    height = sum(row[1] for row in rows) + gap * (len(rows) - 1)
    out = bytearray(bytes((0, 0, 0, 255)) * (width * height))
    y_origin = 0
    for row_width, row_height, rgba in rows:
        for y in range(row_height):
            target = ((y_origin + y) * width) * 4
            out[target : target + row_width * 4] = rgba[y * row_width * 4 : (y + 1) * row_width * 4]
        y_origin += row_height + gap
    return width, height, bytes(out)


def png_files(directory: Path) -> dict[str, Path]:
    return {
        path.relative_to(directory).as_posix(): path
        for path in sorted(directory.rglob("*"))
        if path.is_file() and path.suffix.lower() == ".png"
    }


def differing_bytes(before: bytes, after: bytes) -> int:
    return sum(left != right for left, right in zip(before, after)) + abs(len(before) - len(after))


def compare_pixels(
    before: bytes, after: bytes, width: int, tolerance: int
) -> tuple[int, int, tuple[int, int, int, int] | None]:
    different = 0
    max_delta = 0
    min_x = min_y = None
    max_x = max_y = None
    for offset in range(0, len(before), 4):
        deltas = [abs(before[offset + channel] - after[offset + channel]) for channel in range(4)]
        max_delta = max(max_delta, *deltas)
        if any(delta > tolerance for delta in deltas):
            pixel = offset // 4
            x = pixel % width
            y = pixel // width
            different += 1
            if min_x is None:
                min_x = max_x = x
                min_y = max_y = y
            else:
                min_x = min(min_x, x)
                max_x = max(max_x, x)
                min_y = min(min_y, y)
                max_y = max(max_y, y)
    if min_x is None:
        return different, max_delta, None
    return different, max_delta, (min_x, min_y, max_x, max_y)


def compare_directory_pair(
    before_root: Path,
    after_root: Path,
    max_different_pixels: int,
    channel_tolerance: int,
    require_byte_identical: bool,
    crops_dir: Path | None = None,
    crop_padding: int = 8,
    sheet_path: Path | None = None,
    sheet_row_height: int = 160,
) -> int:
    before_files = png_files(before_root)
    after_files = png_files(after_root)
    if not before_files and not after_files:
        raise ValueError("neither directory contains PNG files")

    failed = False
    had_error = False
    sheet_rows: list[Image] = []
    for relative in sorted(before_files.keys() | after_files.keys()):
        before_path = before_files.get(relative)
        after_path = after_files.get(relative)
        if before_path is None:
            print(f"{relative}: missing before image")
            failed = True
            continue
        if after_path is None:
            print(f"{relative}: missing after image")
            failed = True
            continue

        try:
            before_bytes = before_path.read_bytes()
            after_bytes = after_path.read_bytes()
            before_width, before_height, before_pixels = decode_png(before_path)
            after_width, after_height, after_pixels = decode_png(after_path)
        except (OSError, PngError) as error:
            print(f"{relative}: ERROR {error}", file=sys.stderr)
            had_error = True
            continue

        byte_diff = differing_bytes(before_bytes, after_bytes)
        byte_equal = byte_diff == 0
        if (before_width, before_height) != (after_width, after_height):
            print(
                f"{relative}: byte_equal={'yes' if byte_equal else 'no'} "
                f"byte_diff={byte_diff} dimensions={before_width}x{before_height} "
                f"vs {after_width}x{after_height} pixel_diff=unavailable"
            )
            failed = True
            continue

        different_pixels, max_delta, box = compare_pixels(
            before_pixels, after_pixels, before_width, channel_tolerance
        )
        bounds = "none" if box is None else f"x={box[0]}..{box[2]},y={box[1]}..{box[3]}"
        before_image = (before_width, before_height, before_pixels)
        after_image = (after_width, after_height, after_pixels)
        if crops_dir is not None and box is not None:
            left = max(0, box[0] - crop_padding)
            top = max(0, box[1] - crop_padding)
            right = min(before_width - 1, box[2] + crop_padding)
            bottom = min(before_height - 1, box[3] + crop_padding)
            tiles = [
                crop_image(before_image, left, top, right, bottom),
                crop_image(after_image, left, top, right, bottom),
                crop_image(
                    diff_image(before_image, after_image, channel_tolerance),
                    left, top, right, bottom,
                ),
            ]
            crops_dir.mkdir(parents=True, exist_ok=True)
            crop_path = crops_dir / (Path(relative).with_suffix("").as_posix().replace("/", "__") + ".crop.png")
            crop_path.write_bytes(encode_png(*stack_horizontal(tiles)))
            bounds += f" crop={crop_path}"
        if sheet_path is not None:
            sheet_rows.append(stack_horizontal([
                scale_to_height(tile, sheet_row_height)
                for tile in (before_image, after_image,
                             diff_image(before_image, after_image, channel_tolerance))
            ]))
        print(
            f"{relative}: byte_equal={'yes' if byte_equal else 'no'} "
            f"byte_diff={byte_diff} pixels={before_width}x{before_height} "
            f"pixel_diff={different_pixels} max_channel_delta={max_delta} bbox={bounds}"
        )
        if different_pixels > max_different_pixels or (require_byte_identical and not byte_equal):
            failed = True

    if sheet_path is not None and sheet_rows:
        sheet_path.parent.mkdir(parents=True, exist_ok=True)
        sheet_path.write_bytes(encode_png(*stack_vertical(sheet_rows)))
        print(f"contact sheet: {sheet_path} rows={len(sheet_rows)}")
    if had_error:
        return 2
    return 1 if failed else 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Compare matching PNGs in before/after capture directories."
    )
    parser.add_argument("before", type=Path, help="directory containing before captures")
    parser.add_argument("after", type=Path, help="directory containing after captures")
    parser.add_argument(
        "--max-differing-pixels",
        type=int,
        default=0,
        help="maximum changed pixels allowed per image (default: 0)",
    )
    parser.add_argument(
        "--channel-tolerance",
        type=int,
        default=0,
        help="ignore per-channel absolute deltas up to this value (default: 0)",
    )
    parser.add_argument(
        "--require-byte-identical",
        action="store_true",
        help="also fail when PNG file bytes differ, even if decoded pixels match",
    )
    parser.add_argument(
        "--crops",
        type=Path,
        metavar="DIR",
        help="write a before|after|diff crop of each differing pair's changed region into DIR",
    )
    parser.add_argument(
        "--crop-padding",
        type=int,
        default=8,
        help="pixels of context around the changed region in --crops (default: 8)",
    )
    parser.add_argument(
        "--contact-sheet",
        type=Path,
        metavar="FILE",
        help="write one before|after|diff row per same-size pair into a single PNG",
    )
    parser.add_argument(
        "--reference-provenance",
        type=Path,
        metavar="FILE",
        help="print reference provenance and reject references older than a declared visual change",
    )
    parser.add_argument(
        "--repository",
        type=Path,
        default=Path.cwd(),
        metavar="DIR",
        help="git repository used to resolve provenance commits (default: current directory)",
    )
    parser.add_argument(
        "--source-commit",
        metavar="REV",
        help="tree being compared; defaults to HEAD in --repository",
    )
    parser.add_argument(
        "--sheet-row-height",
        type=int,
        default=160,
        help="height in pixels of each --contact-sheet row (default: 160)",
    )
    args = parser.parse_args()

    if args.crop_padding < 0 or args.sheet_row_height < 1:
        parser.error("--crop-padding must be non-negative and --sheet-row-height positive")
    if args.max_differing_pixels < 0:
        parser.error("--max-differing-pixels must be non-negative")
    if not 0 <= args.channel_tolerance <= 255:
        parser.error("--channel-tolerance must be between 0 and 255")
    for directory in (args.before, args.after):
        if not directory.is_dir():
            parser.error(f"not a directory: {directory}")

    try:
        if args.reference_provenance and check_reference_provenance(
            args.reference_provenance, args.repository, args.source_commit
        ):
            return 1
        return compare_directory_pair(
            args.before,
            args.after,
            args.max_differing_pixels,
            args.channel_tolerance,
            args.require_byte_identical,
            args.crops,
            args.crop_padding,
            args.contact_sheet,
            args.sheet_row_height,
        )
    except (ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
