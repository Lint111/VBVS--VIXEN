"""A split CTest run whose selection matches no tests must fail, never pass."""
from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).parents[1] / "run-tests-split.sh"
TESTFILE = """\
add_test(cpu_one "/bin/true")
set_tests_properties(cpu_one PROPERTIES LABELS "cpu;Suite")
add_test(gpu_one "/bin/true")
set_tests_properties(gpu_one PROPERTIES LABELS "gpu;Suite")
"""


class SplitRunnerSelectionTests(unittest.TestCase):
    def run_split(self, *arguments):
        with tempfile.TemporaryDirectory() as directory:
            Path(directory, "CTestTestfile.cmake").write_text(TESTFILE)
            return subprocess.run(
                ["bash", str(SCRIPT), "--build-dir", directory, "--no-queue", "--label", "Suite", *arguments],
                capture_output=True, text=True)

    def test_zero_selected_fails(self):
        result = self.run_split("--regex", "matches_nothing")
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("matches no tests", result.stderr)

    def test_one_sided_selection_passes(self):
        result = self.run_split("--regex", "^cpu_one$")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("100% tests passed, 0 tests failed out of 1", result.stdout)


if __name__ == "__main__":
    unittest.main()
