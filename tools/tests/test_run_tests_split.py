"""A split CTest run whose selection matches no tests must fail, never pass."""
from pathlib import Path
import os
import shutil
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
    def run_split(self, *arguments, testfile=TESTFILE):
        with tempfile.TemporaryDirectory() as directory:
            Path(directory, "CTestTestfile.cmake").write_text(testfile)
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

    def test_unbuilt_executable_without_edge_is_named_before_running(self):
        testfile = TESTFILE + (
            'add_test(cpu_missing "/nonexistent/test_unbuilt_exe")\n'
            'set_tests_properties(cpu_missing PROPERTIES LABELS "cpu;Suite")\n')
        result = self.run_split(testfile=testfile)
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("/nonexistent/test_unbuilt_exe", result.stderr)
        self.assertIn("no usable exe:<target> build edge", result.stderr)

    def test_selected_executable_target_is_built_before_running(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            (build / "CTestTestfile.cmake").write_text(
                'add_test(cpu_selected "' + str(build / "selected_target") + '")\n'
                'set_tests_properties(cpu_selected PROPERTIES LABELS "cpu;Suite;exe:selected_target")\n'
                'add_test(gpu_one "/bin/true")\n'
                'set_tests_properties(gpu_one PROPERTIES LABELS "gpu;Suite")\n')
            fake_bin = build / "fake-bin"
            fake_bin.mkdir()
            fake_cmake = fake_bin / "cmake"
            fake_cmake.write_text(
                "#!/bin/sh\n"
                "if [ \"$1\" = --build ]; then\n"
                "  printf '#!/bin/sh\\nexit 0\\n' > \"$2/selected_target\"\n"
                "  chmod +x \"$2/selected_target\"\n"
                "  printf '%s\\n' \"$@\" > \"$2/build-args.log\"\n"
                "  exit 0\n"
                "fi\n"
                "exec \"$REAL_CMAKE\" \"$@\"\n")
            fake_cmake.chmod(0o755)
            result = subprocess.run(
                ["bash", str(SCRIPT), "--build-dir", directory, "--no-queue", "--label", "Suite", "--gpu-jobs", "1"],
                env={"PATH": f"{fake_bin}:{os.environ['PATH']}", "REAL_CMAKE": shutil.which("cmake")},
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("building selected test targets: selected_target", result.stdout)
            self.assertIn("selected_target", (build / "build-args.log").read_text())

    def test_regex_selection_builds_placeholder_target_and_keeps_regex(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            (build / "CTestTestfile.cmake").write_text(
                'add_test(cpu_selected "' + str(build / "selected_target") + '")\n'
                'set_tests_properties(cpu_selected PROPERTIES LABELS "cpu;Suite;exe:selected_target")\n')
            fake_bin = build / "fake-bin"
            fake_bin.mkdir()
            fake_cmake = fake_bin / "cmake"
            fake_cmake.write_text(
                "#!/bin/sh\n"
                "if [ \"$1\" = --build ]; then\n"
                "  printf '#!/bin/sh\\nexit 0\\n' > \"$2/selected_target\"\n"
                "  chmod +x \"$2/selected_target\"\n"
                "  printf '%s\\n' \"$@\" > \"$2/build-args.log\"\n"
                "  exit 0\n"
                "fi\n"
                "exec \"$REAL_CMAKE\" \"$@\"\n")
            fake_cmake.chmod(0o755)
            result = subprocess.run(
                ["bash", str(SCRIPT), "--build-dir", directory, "--no-queue", "--label", "Suite",
                 "--regex", "^cpu_selected$", "--gpu-jobs", "1"],
                env={"PATH": f"{fake_bin}:{os.environ['PATH']}", "REAL_CMAKE": shutil.which("cmake")},
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("building selected test targets: selected_target", result.stdout)
            self.assertIn("100% tests passed, 0 tests failed out of 1", result.stdout)

    def test_unbuilt_executable_fails_before_running(self):
        with tempfile.TemporaryDirectory() as directory:
            Path(directory, "CTestTestfile.cmake").write_text(
                TESTFILE + 'add_test(cpu_unbuilt "/nonexistent/never-built")\n'
                'set_tests_properties(cpu_unbuilt PROPERTIES LABELS "cpu;Suite")\n')
            result = subprocess.run(
                ["bash", str(SCRIPT), "--build-dir", directory, "--no-queue", "--label", "Suite"],
                capture_output=True, text=True)
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("cpu_unbuilt -> ", result.stderr)
        self.assertIn("Fix:", result.stderr)
        self.assertNotIn("tests passed", result.stdout)


if __name__ == "__main__":
    unittest.main()
