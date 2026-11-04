"""Test that the light-unify contact sheet is as tall as its complete four-image rows."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "lightunify", Path(__file__).parents[1] / "check-lightunify-captures.py")
lightunify = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lightunify)


def captures(count, width=2, height=3):
    return [(width, height, bytes([value]) * (width*height*4)) for value in range(count)]


class ContactSheetTests(unittest.TestCase):
    def sheet_extent(self, count):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "sheet.png"
            lightunify.sheet(captures(count), path)
            width, height, _ = lightunify.pixels.decode_png(path)
        return width, height

    def test_one_row_is_one_row_tall(self):
        self.assertEqual(self.sheet_extent(4), (8, 3))

    def test_two_rows_are_two_rows_tall(self):
        self.assertEqual(self.sheet_extent(8), (8, 6))

    def test_incomplete_row_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, "multiple of 4"):
                lightunify.sheet(captures(3), Path(directory) / "sheet.png")

def tile(w=2, h=3):
    return (w, h, bytes(w*h*4))


class SheetTest(unittest.TestCase):
    def dims(self, count):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)/"s.png"
            lightunify.sheet([tile() for _ in range(count)], out)
            width, height, rgba = lightunify.pixels.decode_png(out)
            self.assertEqual(len(rgba), width*height*4)
            return width, height

    def test_height_follows_row_count(self):
        self.assertEqual(self.dims(4), (8, 3))
        self.assertEqual(self.dims(8), (8, 6))
        self.assertEqual(self.dims(16), (8, 12))

    def test_incomplete_row_rejected(self):
        with self.assertRaisesRegex(ValueError, "multiple of 4"):
            self.dims(6)


if __name__ == "__main__":
    unittest.main()
