#!/usr/bin/env python3
"""Compare paired PNG captures by file bytes and decoded pixels."""

from __future__ import annotations

import argparse
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
) -> int:
    before_files = png_files(before_root)
    after_files = png_files(after_root)
    if not before_files and not after_files:
        raise ValueError("neither directory contains PNG files")

    failed = False
    had_error = False
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

        different_pixels, max_delta, bounds = compare_pixels(
            before_pixels, after_pixels, before_width, channel_tolerance
        )
        bounds = "none" if bounds is None else (
            f"x={bounds[0]}..{bounds[2]},y={bounds[1]}..{bounds[3]}"
        )
        print(
            f"{relative}: byte_equal={'yes' if byte_equal else 'no'} "
            f"byte_diff={byte_diff} pixels={before_width}x{before_height} "
            f"pixel_diff={different_pixels} max_channel_delta={max_delta} bbox={bounds}"
        )
        if different_pixels > max_different_pixels or (require_byte_identical and not byte_equal):
            failed = True

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
    args = parser.parse_args()

    if args.max_differing_pixels < 0:
        parser.error("--max-differing-pixels must be non-negative")
    if not 0 <= args.channel_tolerance <= 255:
        parser.error("--channel-tolerance must be between 0 and 255")
    for directory in (args.before, args.after):
        if not directory.is_dir():
            parser.error(f"not a directory: {directory}")

    try:
        return compare_directory_pair(
            args.before,
            args.after,
            args.max_differing_pixels,
            args.channel_tolerance,
            args.require_byte_identical,
        )
    except ValueError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
