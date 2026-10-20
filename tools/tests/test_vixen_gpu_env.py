"""Check that standalone GPU harnesses take the Vulkan loader environment from the build cache."""
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).parents[2] / "VIXEN" / "tools"))
from vixen_gpu_env import gpu_test_environment  # noqa: E402


class GpuTestEnvironmentTests(unittest.TestCase):
    def build_tree(self, root, cache_lines):
        build = Path(root) / "build"
        (build / "binaries").mkdir(parents=True)
        (build / "CMakeCache.txt").write_text("".join(f"{line}\n" for line in cache_lines))
        return build / "binaries" / "VIXEN"

    def test_cached_dozen_icd_layer_and_library_dirs_are_exported(self):
        with tempfile.TemporaryDirectory() as root:
            dzn_dir = Path(root) / "dzn"
            dzn_dir.mkdir()
            windowing = Path(root) / "windowing"
            (windowing / "usr" / "lib").mkdir(parents=True)
            app = self.build_tree(root, [
                "VIXEN_WSL_DZN_ICD:INTERNAL=/opt/vixen/dzn_icd.json",
                "VIXEN_VULKAN_LAYER_PATH:PATH=/opt/vixen/explicit_layer.d",
                f"VIXEN_WSL_DZN_LIBRARY_DIR:INTERNAL={dzn_dir}",
                f"VIXEN_WINDOWING_CACHE_DIR:PATH={windowing}",
                f"VULKAN_PATH:PATH={Path(root) / 'missing-sdk'}",
            ])
            with mock.patch.dict(os.environ, {"LD_LIBRARY_PATH": "/opt/existing"}):
                environment = gpu_test_environment(app)
        self.assertEqual(environment["VK_ICD_FILENAMES"], "/opt/vixen/dzn_icd.json")
        self.assertEqual(environment["VK_LAYER_PATH"], "/opt/vixen/explicit_layer.d")
        self.assertEqual(environment["LD_LIBRARY_PATH"],
                         os.pathsep.join([str(dzn_dir), os.path.normpath(str(windowing / "usr" / "lib")),
                                          "/opt/existing"]))

    def test_empty_cache_adds_nothing(self):
        with tempfile.TemporaryDirectory() as root:
            app = self.build_tree(root, [])
            with mock.patch.dict(os.environ, {}, clear=False):
                os.environ.pop("LD_LIBRARY_PATH", None)
                self.assertEqual(gpu_test_environment(app), {})

    def test_app_outside_a_build_tree_is_an_error(self):
        with tempfile.TemporaryDirectory() as root:
            with self.assertRaises(RuntimeError):
                gpu_test_environment(Path(root) / "VIXEN")


if __name__ == "__main__":
    unittest.main()
