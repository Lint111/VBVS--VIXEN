"""vixen-native.sh must hand CTest-style arguments (regex pipes, ampersands) to a batch file intact."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).parents[1] / "vixen-native.sh"
CMD_EXE = Path("/mnt/c/Windows/System32/cmd.exe")
NATIVE_BASE = Path("/mnt/c/vixen-native")


@unittest.skipUnless(CMD_EXE.exists() and NATIVE_BASE.is_dir(), "needs WSL with a Windows-side native root")
class NativeArgumentTests(unittest.TestCase):
    def run_bat(self, *args):
        with tempfile.TemporaryDirectory() as src, tempfile.TemporaryDirectory(dir=NATIVE_BASE, prefix="_argtest-") as base:
            repo = Path(src)
            (repo / "tools").mkdir()
            shutil.copy(SCRIPT, repo / "tools" / "vixen-native.sh")
            (repo / "tools" / "show.bat").write_text(
                "@echo off\r\nsetlocal EnableDelayedExpansion\r\necho GOT=[!VIXEN_NATIVE_ARGS!]\r\n")
            pins = repo / "VIXEN" / ".win-native-toolchain"
            pins.mkdir(parents=True)
            (pins / "ninja-bin.txt").write_text("x")
            (pins / "cmake-exe.txt").write_text("x")
            git = ["git", "-C", str(repo), "-c", "user.name=t", "-c", "user.email=t@t"]
            subprocess.run(git + ["init", "-q", "-b", "lane-argtest"], check=True)
            subprocess.run(git + ["add", "-A"], check=True)
            subprocess.run(git + ["commit", "-q", "-m", "x"], check=True)
            env = dict(os.environ, VIXEN_NATIVE_ROOT=base, VIXEN_NATIVE_SYSTEM32=str(CMD_EXE.parent))
            done = subprocess.run(["bash", str(repo / "tools" / "vixen-native.sh"), "--lane", "argtest",
                                   "--log", str(repo / "log.txt"), "bat", "tools/show.bat", *args],
                                  env=env, capture_output=True, text=True)
            return done

    def test_regex_pipe_reaches_the_batch_file(self):
        done = self.run_bat("-R", "EditorToggleUndoCapture|HudRenderCapture")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn('GOT=["-R" "EditorToggleUndoCapture|HudRenderCapture"]', done.stdout)
        self.assertNotIn("is not recognized", done.stdout)

    def test_other_metacharacters_and_spaces_survive(self):
        done = self.run_bat("-E", "^(a|b)&c$", "x y")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn('GOT=["-E" "^(a|b)&c$" "x y"]', done.stdout)


if __name__ == "__main__":
    unittest.main()
