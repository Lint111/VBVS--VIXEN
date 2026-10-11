#!/usr/bin/env python3
"""Regression checks for available, quantized Vulkan timestamp pairs."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("metric", Path(__file__).with_name("summarize-gpu-yield.py"))
metric = importlib.util.module_from_spec(spec)
spec.loader.exec_module(metric)

class TimestampValidity(unittest.TestCase):
    def summarize(self, start=10, end=30, dispatch_start=20, dispatch_end=20):
        with tempfile.TemporaryDirectory() as folder:
            log = Path(folder) / "producer.log"
            log.write_text(f"[GPUYIELD_CHUNK] start_ticks={start} end_ticks={end} dispatch_start_ticks={dispatch_start} dispatch_end_ticks={dispatch_end} dispatch_gpu_ms=0 elapsed_gpu_ms=0.02\n"
                           "[GPUYIELD] submissions=1 chunks=1 max_submission_ms=0.02 budget_ms=2\n")
            return metric.summarize(log, None, 1)
    def test_available_equal_inner_ticks_preserve_positive_command_witness(self):
        result = self.summarize()
        self.assertTrue(result["budget_pass"])
        self.assertEqual(result["zero_resolution_dispatch_pairs"], 1)
        self.assertEqual(result["invalid_timestamp_pairs"], 0)
    def test_reversed_inner_ticks_fail(self):
        self.assertFalse(self.summarize(dispatch_end=19)["budget_pass"])
    def test_missing_inner_ticks_fail(self):
        self.assertFalse(self.summarize(dispatch_start=0, dispatch_end=0)["budget_pass"])
    def test_empty_outer_interval_fails(self):
        self.assertFalse(self.summarize(end=10)["budget_pass"])

if __name__ == "__main__":
    unittest.main()
