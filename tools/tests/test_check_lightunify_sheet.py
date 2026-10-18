"""Test that the light-unify contact sheet is as tall as its complete four-image rows."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("lightunify", Path(__file__).parents[1] / "check-lightunify-captures.py")
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
            with self.assertRaises(ValueError):
                lightunify.sheet(captures(3), Path(directory) / "sheet.png")


if __name__ == "__main__":
    unittest.main()
