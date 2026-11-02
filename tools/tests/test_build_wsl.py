"""build-wsl.sh routes configure variables to configure and rejects unusable build types."""
import os
import subprocess
import unittest
from pathlib import Path

SCRIPT = Path(__file__).parents[2] / "build-wsl.sh"


def run(*args):
    env = dict(os.environ, BUILD_WSL_DRY_RUN="1")
    return subprocess.run(["bash", str(SCRIPT), *args], env=env, capture_output=True, text=True)


class BuildWslTests(unittest.TestCase):
    def test_definitions_go_to_configure_only(self):
        result = run("-DVIXEN_WITH_FSR3=OFF", "--target", "VIXEN", "-j4")
        self.assertEqual(result.returncode, 0, result.stderr)
        configure, build = [l for l in result.stdout.splitlines() if l.startswith("cmake")]
        self.assertIn("-DVIXEN_WITH_FSR3=OFF", configure)
        self.assertNotIn("-DVIXEN_WITH_FSR3", build)
        self.assertTrue(build.endswith("--target VIXEN -j4"), build)

    def test_release_positional_is_dropped(self):
        result = run("Release")
        build = [l for l in result.stdout.splitlines() if l.startswith("cmake --build")][0]
        self.assertNotIn("Release", build)

    def test_other_build_type_is_rejected_with_fix(self):
        result = run("Debug")
        self.assertEqual(result.returncode, 2)
        self.assertIn("Fix:", result.stderr)
        self.assertEqual(result.stdout, "")


if __name__ == "__main__":
    unittest.main()
