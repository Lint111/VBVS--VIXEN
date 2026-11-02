import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "lightunify", Path(__file__).parents[1] / "check-lightunify-captures.py")
lightunify = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lightunify)


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
