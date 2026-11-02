import os
from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).parents[1] / "run-gtest-filtered.sh"

# A stand-in gtest binary: lists Suite/Case pairs that match the filter (prefix glob only) and "runs" them.
FAKE = r'''#!/usr/bin/env bash
filter=""
list=0
for a in "$@"; do
  case "$a" in
    --gtest_filter=*) filter="${a#--gtest_filter=}" ;;
    --gtest_list_tests) list=1 ;;
  esac
done
cases=("Ray.Tile" "Ray.Miss" "Other.One")
sel=()
for c in "${cases[@]}"; do
  case "$c" in ${filter%\*}*) sel+=("$c") ;; esac
done
if [ "$list" = 1 ]; then
  last=""
  for c in "${sel[@]}"; do
    s="${c%%.*}"; n="${c#*.}"
    if [ "$s" != "$last" ]; then echo "$s."; last="$s"; fi
    echo "  $n  # GetParam() = 1"
  done
  exit 0
fi
echo "Running ${#sel[@]} tests"
exit 0
'''


class RunGtestFilteredTest(unittest.TestCase):
    def run_wrapper(self, *args):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory)/"fake_test"
            binary.write_text(FAKE)
            binary.chmod(0o755)
            return subprocess.run(
                ["bash", str(SCRIPT), *[str(binary) if a == "BIN" else a for a in args]],
                capture_output=True, text=True, stdin=subprocess.DEVNULL)

    def test_selected_tests_run(self):
        result = self.run_wrapper("BIN", "Ray.*")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("2 test(s) selected", result.stdout)

    def test_zero_selected_fails(self):
        result = self.run_wrapper("BIN", "Nothing.*")
        self.assertEqual(result.returncode, 3)
        self.assertIn("selects 0 tests", result.stderr)

    def test_required_case_missing_fails(self):
        result = self.run_wrapper("--require", "Ray.Tile", "--require", "Ray.Gone", "BIN", "Ray.*")
        self.assertEqual(result.returncode, 4)
        self.assertIn("Ray.Gone", result.stderr)

    def test_required_case_present_passes(self):
        result = self.run_wrapper("--require", "Ray.Tile", "BIN", "Ray.*")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_missing_binary_explained(self):
        result = self.run_wrapper("/nonexistent/test_bin", "Ray.*")
        self.assertEqual(result.returncode, 2)
        self.assertIn("build the test target first", result.stderr)


if __name__ == "__main__":
    unittest.main()
