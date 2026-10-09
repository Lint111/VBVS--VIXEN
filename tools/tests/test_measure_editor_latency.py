"""Test the witness's frame correlation and nested CPU span accounting."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

spec = importlib.util.spec_from_file_location("measure", Path(__file__).parents[1] / "measure-editor-latency.py")
measure = importlib.util.module_from_spec(spec)
spec.loader.exec_module(measure)


def image(path, value):
    def chunk(kind, payload):
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))
    path.write_bytes(measure.pixels.PNG_SIGNATURE
        + chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(bytes([0, value, value, value, 255]))) + chunk(b"IEND", b""))


class LatencyWitnessTests(unittest.TestCase):
    def fixture(self, directory, changed_tick):
        lines = []
        def event(phase, edge, wall, tick=None):
            suffix = f" tick={tick} action=0" if tick is not None else ""
            lines.append(f"[RECIPE/latency] phase={phase} edge={edge} wall_us={wall}{suffix}\n")
        image(directory / "editor_capture_29.png", 0)
        for tick in range(30, 34):
            wall = (tick - 30) * 10000
            event("frame", "begin", wall, tick)
            if tick == 30:
                event("dispatch", "begin", wall + 100, tick)
                event("dispatch", "end", wall + 200, tick)
                event("flatten", "begin", wall + 500, tick)
                event("flatten", "end", wall + 700, tick)
            event("render", "begin", wall + 1000)
            if tick == 30:
                event("compile", "begin", wall + 2000)
                event("compile", "end", wall + 4000)
                event("materialize", "begin", wall + 4000)
                event("materialize", "end", wall + 5000)
            event("render", "end", wall + 6000)
            event("frame", "end", wall + 6500, tick)
            image(directory / f"editor_capture_{tick}.png", 1 if changed_tick is not None and tick >= changed_tick else 0)
        (directory / "run.log").write_text("".join(lines))

    def run_fixture(self, changed_tick):
        with tempfile.TemporaryDirectory(dir=measure.REPO / ".tmp") as scratch:
            directory = Path(scratch)
            self.fixture(directory, changed_tick)
            return measure.analyse(directory)["edits"][0]

    def test_first_presented_frame_counts_as_one(self):
        result = self.run_fixture(30)
        self.assertEqual(result["presented_frames_to_change"], 1)
        self.assertEqual(result["dispatch_to_frame_end_ms"], 6.4)
        self.assertEqual(result["dispatch_to_flatten_ms"], 0.3)
        self.assertEqual(result["phases_ms"]["render_exclusive"], 2.0)

    def test_deferred_change_is_not_mistaken_for_next_frame(self):
        result = self.run_fixture(31)
        self.assertEqual(result["presented_frames_to_change"], 2)
        self.assertEqual(result["dispatch_to_frame_end_ms"], 16.4)
        self.assertEqual(result["phase_ticks"]["flatten"], [30])
        self.assertEqual(result["phase_ticks"]["compile"], [30])

    def test_same_tick_dispatches_are_measured_as_one_published_edit(self):
        with tempfile.TemporaryDirectory(dir=measure.REPO / ".tmp") as scratch:
            directory = Path(scratch)
            self.fixture(directory, 31)
            log = directory / "run.log"
            with log.open("a") as stream:
                stream.write("[RECIPE/latency] phase=dispatch edge=begin wall_us=220 tick=30 action=0\n"
                             "[RECIPE/latency] phase=dispatch edge=end wall_us=250 tick=30 action=0\n")
            edits = measure.analyse(directory)["edits"]
            self.assertEqual(len(edits), 1)
            self.assertEqual(edits[0]["dispatch_count"], 2)
            self.assertEqual(edits[0]["dispatch_ms"], 0.15)
            self.assertEqual(edits[0]["dispatch_to_flatten_ms"], 0.25)

    def test_invisible_edit_has_no_fabricated_latency(self):
        result = self.run_fixture(None)
        self.assertIsNone(result["presented_frames_to_change"])
        self.assertIsNone(result["dispatch_to_frame_end_ms"])

    def test_incomplete_trace_is_rejected(self):
        with tempfile.TemporaryDirectory(dir=measure.REPO / ".tmp") as scratch:
            directory = Path(scratch)
            self.fixture(directory, 30)
            with (directory / "run.log").open("a") as log:
                log.write("[RECIPE/latency] phase=bake edge=begin wall_us=99000\n")
            with self.assertRaisesRegex(ValueError, "unclosed spans"):
                measure.analyse(directory)

    def test_live_input_cannot_contaminate_a_scripted_witness(self):
        with tempfile.TemporaryDirectory(dir=measure.REPO / ".tmp") as scratch:
            directory = Path(scratch)
            self.fixture(directory, 31)
            with (directory / "run.log").open("a") as log:
                log.write("[RECIPE/latency] phase=dispatch edge=begin wall_us=10100 tick=31 action=-1\n"
                          "[RECIPE/latency] phase=dispatch edge=end wall_us=10200 tick=31 action=-1\n")
            with self.assertRaisesRegex(ValueError, "live UI dispatch"):
                measure.analyse(directory)

    def test_transitional_pixels_are_not_reported_as_the_settled_edit(self):
        with tempfile.TemporaryDirectory(dir=measure.REPO / ".tmp") as scratch:
            directory = Path(scratch)
            self.fixture(directory, 31)
            image(directory / "editor_capture_30.png", 2)
            result = measure.analyse(directory)["edits"][0]
            self.assertEqual(result["presented_frames_to_change"], 1)
            self.assertEqual(result["settled_match_frames"], 2)


if __name__ == "__main__":
    unittest.main()
