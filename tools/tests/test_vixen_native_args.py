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
    def run_bat(self, *args, layout=False):
        with tempfile.TemporaryDirectory() as src, tempfile.TemporaryDirectory(dir=NATIVE_BASE, prefix="_argtest-") as base:
            home = Path(src) / "home"
            home.mkdir()
            repo = Path(src) / "projects" / "VBVS--VIXEN" / ".claude-worktrees" / "argtest"
            (repo / "tools").mkdir(parents=True)
            shutil.copy(SCRIPT, repo / "tools" / "vixen-native.sh")
            (repo / "tools" / "show.bat").write_text(
                "@echo off\r\nsetlocal EnableDelayedExpansion\r\necho GOT=[!VIXEN_NATIVE_ARGS!]\r\n"
                "echo UT=[!UNDERTOW_ROOT!]\r\necho YK=[!YEROKET_ROOT!]\r\n")
            projects = repo.parents[2]
            if layout:
                schema = projects / "undertow" / ".claude-worktrees" / "argtest" / "core/src/Undertow.Authoring/Schema"
                schema.mkdir(parents=True)
                (schema / "schemas.json").write_text("{}")
                tool = home / "Github/Yeroket-Fantasy/Packages/com.yeroket.utility.kernel-framework/CodegenTool~"
                tool.mkdir(parents=True)
                (tool / "CodegenTool.csproj").write_text("x")
            pins = repo / "VIXEN" / ".win-native-toolchain"
            pins.mkdir(parents=True)
            (pins / "ninja-bin.txt").write_text("x")
            (pins / "cmake-exe.txt").write_text("x")
            git = ["git", "-C", str(repo), "-c", "user.name=t", "-c", "user.email=t@t"]
            subprocess.run(git + ["init", "-q", "-b", "lane-argtest"], check=True)
            subprocess.run(git + ["add", "-A"], check=True)
            subprocess.run(git + ["commit", "-q", "-m", "x"], check=True)
            env = {k: v for k, v in os.environ.items() if k not in ("UNDERTOW_ROOT", "YEROKET_ROOT")}
            env.update(HOME=str(home), VIXEN_NATIVE_ROOT=base, VIXEN_NATIVE_SYSTEM32=str(CMD_EXE.parent))
            self.projects = projects
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

    def test_paired_undertow_and_kernel_roots_are_inferred(self):
        done = self.run_bat("-j", "1", layout=True)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertRegex(done.stdout, r"UT=\[.*undertow.*argtest\]")
        self.assertRegex(done.stdout, r"YK=\[.*Yeroket-Fantasy\]")

    def test_missing_roots_stay_unset(self):
        done = self.run_bat("-j", "1")
        self.assertIn("UT=[]", done.stdout)
        self.assertIn("YK=[]", done.stdout)


if __name__ == "__main__":
    unittest.main()
