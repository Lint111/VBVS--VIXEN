"""Check native-capture's per-run environment overrides: --env validation and CTest env resolution."""
import json
import shutil
import subprocess
import unittest
from pathlib import Path

REPRODUCE = Path(__file__).parents[2] / "reports" / "capturespeed" / "reproduce"
POWERSHELL = Path("/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe")


def run_capture_sh(*args):
    return subprocess.run(["bash", str(REPRODUCE / "native-capture.sh"), *args],
                          capture_output=True, text=True, stdin=subprocess.DEVNULL)


class EnvFlagTests(unittest.TestCase):
    def test_only_vixen_names_are_accepted(self):
        result = run_capture_sh("lookdev", "midday", "--env", "PATH=/x")
        self.assertEqual(result.returncode, 2)
        self.assertIn("--env takes VIXEN_NAME=value", result.stderr)

    def test_semicolon_in_value_is_rejected(self):
        self.assertEqual(run_capture_sh("lookdev", "midday", "--env", "VIXEN_A=1;VIXEN_B=2").returncode, 2)

    def test_missing_value_argument_is_rejected(self):
        self.assertEqual(run_capture_sh("lookdev", "midday", "--env").returncode, 2)

    def test_modes_without_a_single_test_selection_refuse_overrides(self):
        for mode in ("--all-tests", "--negative-missing-dll"):
            result = run_capture_sh(mode, "--env", "VIXEN_A=1")
            self.assertEqual(result.returncode, 2, mode)
            self.assertIn("state and --label", result.stderr)


@unittest.skipUnless(POWERSHELL.exists(), "Windows PowerShell is not reachable")
class ResolveCTestEnvironmentTests(unittest.TestCase):
    def resolve(self, base, environment=(), modification=(), overrides=""):
        helper = subprocess.run(["wslpath", "-w", str(REPRODUCE / "native-capture-env.ps1")],
                                capture_output=True, text=True, check=True).stdout.strip()
        script = (
            f". '{helper}'; "
            f"$base = @{{}}; foreach ($pair in ('{json.dumps(base)}' | ConvertFrom-Json).PSObject.Properties) "
            "{ $base[$pair.Name] = $pair.Value }; "
            f"$o = ConvertTo-NativeCaptureOverrides '{overrides}'; "
            f"$r = Resolve-CTestEnvironment -Base $base -Environment @({','.join(repr(e) for e in environment) or ''}) "
            f"-Modification @({','.join(repr(m) for m in modification) or ''}) -Overrides $o; "
            "$out = [ordered]@{}; foreach ($k in ($r.Keys | Sort-Object)) { $out[$k] = $r[$k] }; "
            "$out | ConvertTo-Json -Compress")
        result = subprocess.run([str(POWERSHELL), "-NoLogo", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-Command", script],
                                capture_output=True, text=True, stdin=subprocess.DEVNULL)
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        return json.loads(result.stdout)

    def test_override_beats_registered_modification(self):
        resolved = self.resolve({"TEMP": "t"}, ["VIXEN_W=1"],
                                ["VIXEN_SHADOW_CONFIG_BIAS_EPSILON=set:0.15"],
                                "VIXEN_SHADOW_CONFIG_BIAS_EPSILON=0.45")
        self.assertEqual(resolved["VIXEN_SHADOW_CONFIG_BIAS_EPSILON"], "0.45")
        self.assertEqual(resolved["VIXEN_W"], "1")
        self.assertEqual(resolved["TEMP"], "t")

    def test_registered_environment_survives_without_override(self):
        resolved = self.resolve({}, ["VIXEN_A=1"], ["VIXEN_B=set:2", "VIXEN_A=unset:"])
        self.assertIsNone(resolved["VIXEN_A"])
        self.assertEqual(resolved["VIXEN_B"], "2")

    def test_string_append_and_reset(self):
        resolved = self.resolve({"VIXEN_C": "base"}, [], ["VIXEN_C=string_append:+x", "VIXEN_C=reset:"])
        self.assertEqual(resolved["VIXEN_C"], "base")

    def test_bad_override_names_fail(self):
        helper = subprocess.run(["wslpath", "-w", str(REPRODUCE / "native-capture-env.ps1")],
                                capture_output=True, text=True, check=True).stdout.strip()
        result = subprocess.run([str(POWERSHELL), "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-Command",
                                 f". '{helper}'; ConvertTo-NativeCaptureOverrides 'PATH=x'"],
                                capture_output=True, text=True, stdin=subprocess.DEVNULL)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("expected VIXEN_NAME=value", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
