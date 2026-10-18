"""Test that the windowed capture wrapper hands its capture directories to the applications as absolute paths."""
from pathlib import Path
import os
import stat
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).parents[1] / "run-vixen-windowed-captures.sh"

# Stands in for a capture binary: writes a non-empty PNG for each frame named in its capture-dir variable.
FAKE_APP = """#!/usr/bin/env bash
if [[ -n "${VIXEN_HUD_CAPTURE_DIR:-}" ]]; then
    printf '%s\\n' "$VIXEN_HUD_CAPTURE_DIR" > "$VIXEN_HUD_CAPTURE_DIR/dir.seen"
    for frame in 5 45 75; do printf 'png' > "$VIXEN_HUD_CAPTURE_DIR/hud_capture_$frame.png"; done
fi
if [[ -n "${VIXEN_EDITOR_CAPTURE_DIR:-}" ]]; then
    for frame in 5 45 75 105; do printf 'png' > "$VIXEN_EDITOR_CAPTURE_DIR/editor_capture_$frame.png"; done
fi
"""


def capture_images(directory):
    return [
        directory / "hud" / "hud_capture_5.png",
        directory / "hud" / "hud_capture_45.png",
        directory / "hud" / "hud_capture_75.png",
        directory / "editor" / "editor_capture_5.png",
        directory / "editor" / "editor_capture_45.png",
        directory / "editor" / "editor_capture_75.png",
        directory / "editor" / "editor_capture_105.png",
    ]


class WindowedCaptureTests(unittest.TestCase):
    def setUp(self):
        x_socket = Path("/tmp/.X11-unix/X0")
        if not x_socket.is_socket():
            self.skipTest("needs a local X11 socket on display :0")

    def test_relative_capture_root_is_resolved_before_apps_launch(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            runtime = root / "bin"
            runtime.mkdir()
            for name in ("vixen", "editor"):
                app = runtime / name
                app.write_text(FAKE_APP)
                app.chmod(app.stat().st_mode | stat.S_IXUSR)
            work = root / "work"
            work.mkdir()

            result = subprocess.run(
                ["bash", str(SCRIPT), str(runtime), "vixen", str(runtime), "editor", "captures"],
                cwd=work, env={**os.environ, "DISPLAY": ":0"}, capture_output=True, text=True, timeout=120)

            self.assertEqual(result.returncode, 0, result.stderr)
            for image in capture_images(work / "captures"):
                self.assertGreater(image.stat().st_size, 0, f"missing capture {image}")
            seen = (work / "captures" / "hud" / "dir.seen").read_text().strip()
            self.assertEqual(seen, str(work / "captures" / "hud"))


if __name__ == "__main__":
    unittest.main()
