"""Check that the windowed capture runner gives its apps the CTest Vulkan environment."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

RUNNER = Path(__file__).parents[1] / "run-vixen-windowed-captures.sh"

STUB = """#!/usr/bin/env python3
import os
import pathlib

with open(os.environ["STUB_RECORD"], "a") as record:
    record.write(f"{os.environ.get('VK_ICD_FILENAMES', 'unset')} {os.environ.get('VK_LAYER_PATH', 'unset')}\\n")
for variable, names in (
        ("VIXEN_HUD_CAPTURE_DIR", ["hud_capture_5.png", "hud_capture_45.png", "hud_capture_75.png"]),
        ("VIXEN_EDITOR_CAPTURE_DIR", ["editor_capture_5.png", "editor_capture_45.png",
                                      "editor_capture_75.png", "editor_capture_105.png"])):
    if variable in os.environ:
        for name in names:
            pathlib.Path(os.environ[variable], name).write_bytes(b"png")
"""


@unittest.skipUnless(os.path.exists("/tmp/.X11-unix/X0"), "the runner needs a local X11 socket on :0")
class CaptureRunnerVulkanEnvironmentTests(unittest.TestCase):
    def run_runner(self, cache_entries, inherited_icd):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            build = root / "build"
            runtime = build / "runtime"
            editor = build / "editor"
            runtime.mkdir(parents=True)
            editor.mkdir()
            for directory, name in ((runtime, "VIXEN"), (editor, "vixen_editor")):
                executable = directory / name
                executable.write_text(STUB)
                executable.chmod(0o755)
            (build / "CMakeCache.txt").write_text(
                "".join(f"{key}:{kind}={value}\n" for key, (kind, value) in cache_entries.items()))
            record = root / "record.txt"
            env = dict(os.environ, STUB_RECORD=str(record))
            env.pop("VK_LAYER_PATH", None)
            if inherited_icd is None:
                env.pop("VK_ICD_FILENAMES", None)
            else:
                env["VK_ICD_FILENAMES"] = inherited_icd
            result = subprocess.run(
                ["bash", str(RUNNER), str(runtime), "VIXEN", str(editor), "vixen_editor", str(root / "captures")],
                env=env, capture_output=True, text=True)
            lines = record.read_text().splitlines() if record.exists() else []
            return result, lines

    def test_provisioned_dozen_icd_and_layer_path_override_inherited_values(self):
        result, lines = self.run_runner(
            {
                "VIXEN_WSL_DZN_ICD": ("INTERNAL", "/opt/vixen/dzn_icd.json"),
                "VIXEN_VULKAN_LAYER_PATH": ("PATH", "/opt/vixen/explicit_layer.d"),
            },
            inherited_icd="/opt/stale/other_icd.json")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(lines, ["/opt/vixen/dzn_icd.json /opt/vixen/explicit_layer.d"] * 2)

    def test_inherited_icd_is_kept_when_no_dozen_is_provisioned(self):
        result, lines = self.run_runner({}, inherited_icd="/opt/inherited/icd.json")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(lines, ["/opt/inherited/icd.json unset"] * 2)


if __name__ == "__main__":
    unittest.main()
