"""Test the Pillow-free PNG comparison, crop and contact-sheet toolchain."""
import ast
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest

TOOL = Path(__file__).parents[1] / "compare-capture-pixels.py"
spec = importlib.util.spec_from_file_location("compare_capture_pixels", TOOL)
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


def solid(width, height, rgba):
    return width, height, bytes(rgba) * (width * height)


def with_patch(image, left, top, right, bottom, rgba):
    width, height, pixels = image
    out = bytearray(pixels)
    for y in range(top, bottom + 1):
        for x in range(left, right + 1):
            out[(y * width + x) * 4:(y * width + x) * 4 + 4] = bytes(rgba)
    return width, height, bytes(out)


def write(path, image):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(tool.encode_png(*image))


class CompareCapturePixelsTests(unittest.TestCase):
    def run_tool(self, before, after, *extra):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = tool.compare_directory_pair(before, after, 0, 0, False, *extra)
        return code, out.getvalue()

    def test_imports_stay_in_the_standard_library(self):
        tree = ast.parse(TOOL.read_text())
        roots = {alias.name.split(".")[0] for node in ast.walk(tree) if isinstance(node, ast.Import)
                 for alias in node.names}
        roots |= {node.module.split(".")[0] for node in ast.walk(tree)
                  if isinstance(node, ast.ImportFrom) and node.module}
        self.assertTrue(roots <= set(sys.stdlib_module_names), roots - set(sys.stdlib_module_names))

    def test_encode_decode_round_trip(self):
        image = with_patch(solid(5, 4, (10, 20, 30, 255)), 1, 1, 2, 2, (200, 100, 50, 128))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "a.png"
            write(path, image)
            self.assertEqual(tool.decode_png(path), image)

    def test_metrics_crop_and_sheet_for_a_known_pair(self):
        base = solid(40, 30, (50, 60, 70, 255))
        changed = with_patch(base, 10, 12, 14, 15, (90, 60, 70, 255))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write(root / "before/scene/a.png", base)
            write(root / "after/scene/a.png", changed)
            write(root / "before/b.png", base)
            write(root / "after/b.png", base)
            crops, sheet = root / "crops", root / "sheet.png"
            code, text = self.run_tool(root / "before", root / "after", crops, 3, sheet, 20)

            self.assertEqual(code, 1)
            self.assertIn("scene/a.png: byte_equal=no", text)
            self.assertIn("pixel_diff=20 max_channel_delta=40 bbox=x=10..14,y=12..15", text)
            self.assertIn("b.png: byte_equal=yes byte_diff=0 pixels=40x30 pixel_diff=0", text)

            crop = tool.decode_png(crops / "scene__a.crop.png")
            # bbox x 10..14 (5 wide), y 12..15 (4 tall) plus 3 padding: x 7..17, y 9..18
            tile_width, tile_height = 11, 10
            self.assertEqual(crop[:2], (tile_width * 3 + 4, tile_height))
            row = crop[2]
            after_tile_origin = (tile_width + 2) * 4
            patch_pixel = ((12 - 9) * crop[0] + (tile_width + 2) + (10 - 7)) * 4
            self.assertEqual(row[patch_pixel:patch_pixel + 4], bytes((90, 60, 70, 255)))
            self.assertEqual(row[after_tile_origin:after_tile_origin + 4], bytes((50, 60, 70, 255)))
            diff_pixel = ((12 - 9) * crop[0] + 2 * (tile_width + 2) + (10 - 7)) * 4
            self.assertEqual(row[diff_pixel:diff_pixel + 3], bytes((255, 235, 0)))
            self.assertFalse((crops / "b.crop.png").exists())

            width, height, _ = tool.decode_png(sheet)
            self.assertEqual((width, height), (40 * 20 // 30 * 3 + 4, 20 * 2 + 4))
            self.assertIn("rows=2", text)

    def test_size_mismatch_fails_without_a_crop_or_sheet_row(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write(root / "before/a.png", solid(8, 8, (0, 0, 0, 255)))
            write(root / "after/a.png", solid(9, 8, (0, 0, 0, 255)))
            code, text = self.run_tool(root / "before", root / "after", root / "crops", 8, root / "s.png", 16)
            self.assertEqual(code, 1)
            self.assertIn("pixel_diff=unavailable", text)
            self.assertFalse((root / "crops").exists())
            self.assertFalse((root / "s.png").exists())

    def test_reference_provenance_is_printed_for_current_reference(self):
        repository = Path(__file__).parents[2]
        source_commit = tool._git_commit(repository, "HEAD")
        with tempfile.TemporaryDirectory() as directory:
            provenance = Path(directory) / "reference-provenance.json"
            provenance.write_text(json.dumps({
                "source_commit": source_commit,
                "captured_at": "2026-10-25T13:11:00+03:00",
                "machine": "test-machine",
                "driver": {"name": "test-driver"},
                "resolution": [500, 500],
                "known_visual_changes": [],
            }), encoding="utf-8")
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                self.assertFalse(tool.check_reference_provenance(provenance, repository, source_commit))
            self.assertIn(f"reference provenance: commit={source_commit}", out.getvalue())
            self.assertIn("machine=test-machine", out.getvalue())

    def test_reference_provenance_rejects_a_known_visual_change(self):
        repository = Path(__file__).parents[2]
        with tempfile.TemporaryDirectory() as directory:
            provenance = Path(directory) / "reference-provenance.json"
            provenance.write_text(json.dumps({
                "source_commit": "0e405d71ec4b8f93d9d2ec34aa747d6d91eb702a",
                "known_visual_changes": [{
                    "commit": "bff5507184efcfb6bbbcaf6a079d1e28b741f2fc",
                    "title": "affine look-dev instance placement",
                }],
            }), encoding="utf-8")
            out, err = io.StringIO(), io.StringIO()
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                self.assertTrue(tool.check_reference_provenance(provenance, repository, "HEAD"))
            self.assertIn("references older than", err.getvalue())
            self.assertIn("affine look-dev instance placement", err.getvalue())


if __name__ == "__main__":
    unittest.main()
