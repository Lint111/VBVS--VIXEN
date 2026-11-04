"""The GPU ctest guard must release a stalled run quickly and name the first cause (T-7061)."""
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest

GUARD = Path(__file__).parents[1] / "ctest-gpu-guard.py"


class GpuGuardTests(unittest.TestCase):
    def run_guard(self, script_body, *options):
        with tempfile.TemporaryDirectory() as directory:
            script = Path(directory, "fake-ctest.sh")
            script.write_text("#!/usr/bin/env bash\n" + script_body)
            started = time.monotonic()
            result = subprocess.run([sys.executable, str(GUARD), *options, "--", "bash", str(script)],
                                    capture_output=True, text=True, timeout=60)
            return result, time.monotonic() - started

    def test_device_loss_stops_the_run_with_first_cause(self):
        result, wall = self.run_guard(
            "echo '      Start  1: gpu_a'\n"
            "echo 'vkQueueSubmit failed: VK_ERROR_DEVICE_LOST'\n"
            "sleep 300\n")
        self.assertEqual(result.returncode, 3, result.stdout + result.stderr)
        self.assertIn("device loss while running 'gpu_a'", result.stdout)
        self.assertIn("fix:", result.stdout)
        self.assertLess(wall, 30)

    def test_consecutive_timeouts_stop_the_run(self):
        result, wall = self.run_guard(
            "echo '1/9 Test #1: gpu_a ...............***Timeout 600.01 sec'\n"
            "echo '2/9 Test #2: gpu_b ...............***Timeout 600.01 sec'\n"
            "sleep 300\n", "--max-timeouts", "2")
        self.assertEqual(result.returncode, 3, result.stdout + result.stderr)
        self.assertIn("2 consecutive case timeouts (gpu_a, gpu_b)", result.stdout)
        self.assertLess(wall, 30)

    def test_a_pass_resets_the_timeout_streak(self):
        result, _ = self.run_guard(
            "echo '1/3 Test #1: gpu_a ...............***Timeout 600.01 sec'\n"
            "echo '2/3 Test #2: gpu_b ...............   Passed    0.10 sec'\n"
            "echo '3/3 Test #3: gpu_c ...............***Timeout 600.01 sec'\n"
            "exit 8\n", "--max-timeouts", "2")
        self.assertEqual(result.returncode, 8, result.stdout + result.stderr)

    def test_silence_stops_the_run(self):
        result, wall = self.run_guard("echo '      Start  1: gpu_a'\nsleep 300\n", "--idle-seconds", "2")
        self.assertEqual(result.returncode, 3, result.stdout + result.stderr)
        self.assertIn("no ctest output for 2s while running 'gpu_a'", result.stdout)
        self.assertLess(wall, 30)

    def test_healthy_run_passes_status_through(self):
        result, _ = self.run_guard("echo '1/1 Test #1: gpu_a ...............   Passed    0.10 sec'\nexit 0\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Passed", result.stdout)


if __name__ == "__main__":
    unittest.main()
