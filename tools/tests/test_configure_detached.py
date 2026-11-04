"""Check configure-detached.sh resolves its configure inputs from the worktree layout (dry run only)."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).parents[1] / "configure-detached.sh"
SCHEMA = Path("core/src/Undertow.Authoring/Schema/schemas.json")
CODEGEN = Path("Packages/com.yeroket.utility.kernel-framework/CodegenTool~/CodegenTool.csproj")


def touch(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("")


class ConfigureDetachedTests(unittest.TestCase):
    def layout(self, root, paired=True):
        projects = Path(root) / "projects"
        lane = projects / "VIXEN-repo" / ".claude-worktrees" / "lane1"
        (lane / "tools").mkdir(parents=True)
        subprocess.run(["git", "init", "-q", str(lane)], check=True)
        subprocess.run(["git", "-C", str(lane), "-c", "user.name=t", "-c", "user.email=t@t",
                        "commit", "-q", "--allow-empty", "-m", "base"], check=True)
        script = lane / "tools" / "configure-detached.sh"
        script.write_bytes(SCRIPT.read_bytes())
        script.chmod(0o755)
        touch(projects / "undertow" / SCHEMA)
        touch(projects / "Yeroket-Fantasy" / CODEGEN)
        if paired:
            touch(projects / "undertow" / ".claude-worktrees" / "lane1" / SCHEMA)
        return projects, lane, script

    def run_script(self, script, *args, **env):
        base = {k: v for k, v in os.environ.items()
                if k not in ("UNDERTOW_ROOT", "YEROKET_ROOT", "VIXEN_FETCHCONTENT_CACHE", "VIXEN_VULKAN_CACHE_DIR")}
        base.update(env)
        return subprocess.run([str(script), "--dry-run", *args], env=base, text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    def test_inputs_come_from_paired_worktrees_and_shared_caches(self):
        with tempfile.TemporaryDirectory() as root:
            projects, lane, script = self.layout(root)
            result = self.run_script(script, "HEAD", "--kernel-sha", "abc123",
                                     XDG_CACHE_HOME=str(Path(root) / "xdg"))
            self.assertEqual(result.returncode, 0, result.stdout)
            paired = projects / "undertow" / ".claude-worktrees" / "lane1"
            self.assertIn(f"-DVIXEN_SCHEMA_CATALOG={paired / SCHEMA}", result.stdout)
            self.assertIn(f"-DYEROKET_ROOT={projects / 'Yeroket-Fantasy'}", result.stdout)
            self.assertIn(f"-DFETCHCONTENT_BASE_DIR={lane / '.tmp' / 'fetch'}", result.stdout)
            self.assertIn(f"-DVIXEN_VULKAN_CACHE_DIR={Path(root) / 'xdg' / 'vixen' / 'vulkan-sdk'}", result.stdout)
            self.assertIn("-DVIXEN_YEROKET_KERNEL_SHA_OVERRIDE=abc123", result.stdout)

    def test_unpaired_lane_falls_back_and_environment_overrides_win(self):
        with tempfile.TemporaryDirectory() as root:
            projects, lane, script = self.layout(root, paired=False)
            result = self.run_script(script, "HEAD")
            self.assertIn(f"-DVIXEN_SCHEMA_CATALOG={projects / 'undertow' / SCHEMA}", result.stdout)
            self.assertNotIn("KERNEL_SHA_OVERRIDE", result.stdout.split("cmake:")[1])
            custom = Path(root) / "fetch"
            result = self.run_script(script, "HEAD", VIXEN_FETCHCONTENT_CACHE=str(custom))
            self.assertIn(f"-DFETCHCONTENT_BASE_DIR={custom}", result.stdout)

    def test_bad_inputs_fail_with_a_named_reason(self):
        with tempfile.TemporaryDirectory() as root:
            _, _, script = self.layout(root)
            result = self.run_script(script, "no-such-commit")
            self.assertEqual(result.returncode, 2)
            self.assertIn("not a commit", result.stdout)
            result = self.run_script(script, "HEAD", YEROKET_ROOT=str(Path(root) / "missing"))
            self.assertEqual(result.returncode, 2)
            self.assertIn("YEROKET_ROOT", result.stdout)


if __name__ == "__main__":
    unittest.main()
