"""Check native-capture's -CacheSeed: argument validation and the copy-not-share cache seeding."""
import subprocess
import tempfile
import unittest
from pathlib import Path

REPRODUCE = Path(__file__).parents[2] / "reports" / "capturespeed" / "reproduce"
POWERSHELL = Path("/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe")
SCRATCH_PARENT = Path("/mnt/c/vixen-native")
SHA = "a" * 40


def run_capture_sh(*args):
    return subprocess.run(["bash", str(REPRODUCE / "native-capture.sh"), *args],
                          capture_output=True, text=True, stdin=subprocess.DEVNULL)


def win(path):
    return subprocess.run(["wslpath", "-w", str(path)], capture_output=True, text=True, check=True).stdout.strip()


class CacheSeedFlagTests(unittest.TestCase):
    def test_missing_value_is_rejected(self):
        self.assertEqual(run_capture_sh("lookdev", "midday", "--cache-seed").returncode, 2)

    def test_unknown_seed_names_what_it_accepts(self):
        result = run_capture_sh("lookdev", "midday", "--cache-seed", "not-a-rev-or-dir")
        self.assertEqual(result.returncode, 2)
        self.assertIn("neither a directory nor a commit", result.stderr)

    def test_negative_missing_dll_refuses_a_seed(self):
        result = run_capture_sh("--negative-missing-dll", "--cache-seed", "HEAD")
        self.assertEqual(result.returncode, 2)
        self.assertIn("--cache-seed does not apply", result.stderr)


@unittest.skipUnless(POWERSHELL.exists() and SCRATCH_PARENT.exists(), "Windows PowerShell or C:\\vixen-native is not reachable")
class CopyCacheSeedTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(dir=SCRATCH_PARENT, prefix="cacheseed-test-")
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name)
        self.seed = self.root / "build" / "native" / SHA
        self.build = self.root / "build" / "native" / ("b" * 40)
        self.build.mkdir(parents=True)

    def put(self, base, relative, text):
        path = base / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        return path

    def ps(self, body):
        script = f". '{win(REPRODUCE / 'native-capture-cache.ps1')}'; {body}"
        return subprocess.run([str(POWERSHELL), "-NoLogo", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
                               "-Command", script], capture_output=True, text=True, stdin=subprocess.DEVNULL)

    def copy(self):
        result = self.ps(f"$r = Copy-NativeCacheSeed -SeedBuildDir '{win(self.seed)}' -BuildDir '{win(self.build)}'; "
                         "\"files=$($r.Files) bytes=$($r.Bytes) trees=$($r.Trees)\"")
        return result

    def test_runtime_and_per_capture_caches_are_copied_not_linked(self):
        source = self.put(self.seed, "runtime-cache/shared/pipeline-cache/Device_1002_1586/glyph.bin", "blob")
        self.put(self.seed, "runtime-captures/glyphsdf/latin-carved/cache-r768-canonicalabi/devices/x/y.cache", "yy")
        self.put(self.seed, "runtime-captures/glyphsdf/latin-carved/shared-cache-r768-canonicalabi/spirv/a.spv", "a")
        self.put(self.seed, "runtime-captures/glyphsdf/latin-carved/capture.png", "not a cache")
        self.put(self.seed, "bin/vixen.exe", "not a cache")
        result = self.copy()
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        self.assertIn("files=3 bytes=7 trees=3", result.stdout)
        copied = self.build / "runtime-cache/shared/pipeline-cache/Device_1002_1586/glyph.bin"
        self.assertEqual(copied.read_text(), "blob")
        copied.write_text("changed")
        self.assertEqual(source.read_text(), "blob", "the seed must be copied, not shared with the new build")
        self.assertTrue((self.build / "runtime-captures/glyphsdf/latin-carved/shared-cache-r768-canonicalabi/spirv/a.spv").exists())
        self.assertFalse((self.build / "runtime-captures/glyphsdf/latin-carved/capture.png").exists())
        self.assertFalse((self.build / "bin").exists())

    def test_existing_entries_are_not_overwritten(self):
        self.put(self.seed, "runtime-cache/a.cache", "old")
        kept = self.put(self.build, "runtime-cache/a.cache", "warm")
        result = self.copy()
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        self.assertIn("files=0", result.stdout)
        self.assertEqual(kept.read_text(), "warm")

    def test_seed_without_caches_fails_with_location_and_reason(self):
        self.put(self.seed, "bin/vixen.exe", "x")
        result = self.copy()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("cache seed holds no cache", result.stdout + result.stderr)

    def test_commit_seed_resolves_under_the_native_root(self):
        self.put(self.seed, "runtime-cache/a.cache", "x")
        result = self.ps(f"Resolve-NativeCacheSeedBuildDir -Seed '{SHA}' -Root '{win(self.root)}' -BuildDir '{win(self.build)}'")
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        self.assertEqual(result.stdout.strip().lower(), win(self.seed).lower())

    def test_missing_commit_names_the_expected_path(self):
        result = self.ps(f"Resolve-NativeCacheSeedBuildDir -Seed '{'c' * 40}' -Root '{win(self.root)}' -BuildDir '{win(self.build)}'")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("no native build of VIXEN commit", result.stdout + result.stderr)

    def test_seed_equal_to_the_build_is_refused(self):
        result = self.ps(f"Resolve-NativeCacheSeedBuildDir -Seed '{win(self.build)}' -Root '{win(self.root)}' -BuildDir '{win(self.build)}'")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("is the build being run", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
