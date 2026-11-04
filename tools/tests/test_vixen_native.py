"""Check that tools/vixen-native.sh refuses to run from inside the Windows-native clone."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).parents[1] / "vixen-native.sh"


class NativeWrapperCloneGuardTests(unittest.TestCase):
    def run_wrapper_copy(self, native_base, lane, *args):
        lane_dir = Path(native_base) / lane
        (lane_dir / "tools").mkdir(parents=True)
        shutil.copy(SCRIPT, lane_dir / "tools" / "vixen-native.sh")
        env = dict(os.environ, VIXEN_NATIVE_ROOT=str(native_base))
        return subprocess.run(
            ["bash", str(lane_dir / "tools" / "vixen-native.sh"), *args],
            capture_output=True, text=True, env=env)

    def test_wrapper_inside_native_clone_is_rejected_with_fix(self):
        with tempfile.TemporaryDirectory() as root:
            result = self.run_wrapper_copy(root, "lane-x", "sync")
        self.assertEqual(result.returncode, 2)
        self.assertIn("not the native clone", result.stderr)
        self.assertIn("source worktree", result.stderr)

    def test_direct_mode_is_not_rejected_by_the_clone_guard(self):
        with tempfile.TemporaryDirectory() as root:
            result = self.run_wrapper_copy(root, "lane-x", "--direct", "provision")
        self.assertNotIn("not the native clone", result.stderr)


if __name__ == "__main__":
    unittest.main()
