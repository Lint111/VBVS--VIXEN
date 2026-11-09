"""Long CTest repeat gates run as short serial batches and resume from a marker (T-2657)."""
from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).parents[1] / "ctest-repeat-batches.sh"


class RepeatBatchTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.counter = self.dir / "count"
        self.counter.write_text("0")
        self.fail_at = self.dir / "fail_at"
        # Each ctest process appends one line per run; failing when the cumulative count hits fail_at.
        script = self.dir / "t.sh"
        script.write_text(
            f'echo x >> "{self.dir}/runs"\n'
            f'n=$(wc -l < "{self.dir}/runs")\n'
            f'[ -f "{self.fail_at}" ] && [ "$n" -eq "$(cat {self.fail_at})" ] && exit 1\n'
            'exit 0\n')
        (self.dir / "CTestTestfile.cmake").write_text(
            f'add_test(flaky "/bin/sh" "{script}")\n')

    def tearDown(self):
        self.tmp.cleanup()

    def run_gate(self, *extra):
        return subprocess.run(
            ["bash", str(SCRIPT), "--build-dir", str(self.dir), "--no-queue", "--regex", "^flaky$",
             "--total", "7", "--batch", "3", *extra], capture_output=True, text=True)

    def runs(self):
        path = self.dir / "runs"
        return len(path.read_text().splitlines()) if path.exists() else 0

    def test_all_batches_pass(self):
        result = self.run_gate()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.runs(), 7)
        self.assertIn("PASSED 7/7", result.stdout)
        self.assertEqual(result.stdout.count("batch runs"), 3)

    def test_failure_leaves_marker_and_rerun_resumes(self):
        self.fail_at.write_text("5")
        failed = self.run_gate()
        self.assertNotEqual(failed.returncode, 0)
        self.assertIn("3/7 completed", failed.stderr)
        self.fail_at.unlink()
        before = self.runs()
        resumed = self.run_gate()
        self.assertEqual(resumed.returncode, 0, resumed.stdout + resumed.stderr)
        self.assertIn("resuming at 3/7", resumed.stdout)
        self.assertEqual(self.runs() - before, 4)

    def test_no_match_fails(self):
        result = subprocess.run(
            ["bash", str(SCRIPT), "--build-dir", str(self.dir), "--no-queue", "--regex", "nothing"],
            capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("matches no tests", result.stderr)


if __name__ == "__main__":
    unittest.main()
