"""run_with_phase_progress.ps1 keeps a CPU-busy native command visible to the queue watchdog and
stays silent for a command that stopped using CPU (a dead payload must still be reportable)."""
from pathlib import Path
import shutil
import subprocess
import unittest

SCRIPT = Path(__file__).parents[2] / "VIXEN/scripts/build/run_with_phase_progress.ps1"
POWERSHELL = shutil.which("powershell.exe") or "/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe"
BUSY = "$e=(Get-Date).AddSeconds(9); while((Get-Date) -lt $e){ $x=1..2000 | % { $_*$_ } }"


@unittest.skipUnless(Path(POWERSHELL).exists(), "Windows PowerShell is not reachable from this host")
class PhaseProgressTests(unittest.TestCase):
    def run_phase(self, command, heartbeat="2", grace="4"):
        windows_script = subprocess.run(["wslpath", "-w", str(SCRIPT)], capture_output=True, text=True, check=True).stdout.strip()
        return subprocess.run(
            [POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", windows_script, "-Phase", "unit",
             "-HeartbeatSeconds", heartbeat, "-IdleGraceSeconds", grace, "-Exe", command[0], "-ArgLine", " ".join(command[1:])],
            capture_output=True, text=True, stdin=subprocess.DEVNULL, timeout=120)

    def test_busy_command_heartbeats_and_keeps_exit_code(self):
        result = self.run_phase(["powershell.exe", "-NoProfile", "-Command", '"' + BUSY + '; exit 3"'])
        self.assertEqual(result.returncode, 3, result.stdout + result.stderr)
        self.assertIn("[phase:unit] begin:", result.stdout)
        self.assertIn("[phase:unit] alive elapsed=", result.stdout)
        self.assertNotIn("NO CPU PROGRESS", result.stdout)
        self.assertIn("[phase:unit] end: exit=3", result.stdout)

    def test_idle_command_stops_heartbeat(self):
        result = self.run_phase(["ping.exe", "-n", "16", "127.0.0.1"])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stdout.count("NO CPU PROGRESS"), 1, result.stdout)
        tail = result.stdout.split("NO CPU PROGRESS", 1)[1]
        self.assertNotIn("[phase:unit] alive", tail, result.stdout)

    def test_missing_command_is_clear(self):
        result = self.run_phase(["definitely-not-a-command.exe"])
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("could not start", result.stdout)


if __name__ == "__main__":
    unittest.main()
