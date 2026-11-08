"""Test the A/B/A timing driver's schedule, parsing and stability verdict."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("abab", Path(__file__).parents[1] / "measure-abab-timing.py")
abab = importlib.util.module_from_spec(spec)
spec.loader.exec_module(abab)


class AbabTests(unittest.TestCase):
    def test_schedule_alternates_direction(self):
        self.assertEqual(abab.schedule(["a", "b"], 3), [["a", "b"], ["b", "a"], ["a", "b"]])

    def test_variant_parsing_and_error(self):
        self.assertEqual(abab.parse_variant("x=A=1,B=2"), ("x", {"A": "1", "B": "2"}))
        with self.assertRaises(Exception):
            abab.parse_variant("novalue")

    def test_timing_parse_takes_median_of_lines(self):
        text = "[CAPTURE_TIMING] frames=3 frame_p50_ms=10.00 frame_p95_ms=12.0\n" \
               "[CAPTURE_TIMING] frames=3 frame_p50_ms=30.00 frame_p95_ms=40.0\n" \
               "[CAPTURE_TIMING] frames=3 frame_p50_ms=20.00 frame_p95_ms=15.0\n"
        parsed = abab.parse_timing(text)
        self.assertEqual((parsed["p50_ms"], parsed["p95_ms"]), (20.0, 40.0))
        with self.assertRaises(ValueError):
            abab.parse_timing("nothing")

    def test_judge_flags_order_sensitive_data(self):
        _, verdict, _ = abab.judge({"a": [10.7, 28.6], "b": [27.5, 26.5]}, 10.0)
        self.assertEqual(verdict, "INCONCLUSIVE")
        _, verdict, reason = abab.judge({"a": [10.0, 10.1, 10.0], "b": [20.0, 20.2, 20.1]}, 10.0)
        self.assertEqual(verdict, "RANKED")
        self.assertTrue(reason.startswith("a < b"))
        _, verdict, _ = abab.judge({"a": [10.0, 10.5], "b": [10.2, 10.4]}, 10.0)
        self.assertEqual(verdict, "INCONCLUSIVE")

    def test_end_to_end_with_fake_runner(self):
        fake = ("import os;print('[CAPTURE_TIMING] frames=1 frame_p50_ms=%s frame_p95_ms=9' % "
                "('5.0' if os.environ['V']=='a' else '8.0'))")
        with tempfile.TemporaryDirectory() as out:
            code = abab.main(["--variant", "a=V=a", "--variant", "b=V=b", "--output", out,
                              "--telemetry-cmd", "echo '{\"sm_clock_mhz\": 2000}'",
                              "--", sys.executable, "-c", fake])
            report = json.loads((Path(out) / "abab.json").read_text())
        self.assertEqual(code, 0)
        self.assertEqual(report["verdict"], "RANKED")
        self.assertEqual([r["variant"] for r in report["runs"]], ["a", "b", "b", "a", "a", "b"])
        self.assertTrue(report["device_telemetry"])
        self.assertEqual(report["runs"][0]["device_before"], {"sm_clock_mhz": 2000})


if __name__ == "__main__":
    unittest.main()
