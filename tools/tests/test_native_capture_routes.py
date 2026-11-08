"""capture-routes.tsv is the one route table; the shell and PowerShell runners must resolve it identically."""
from pathlib import Path
import json
import re
import shutil
import subprocess
import unittest

REPRODUCE = Path(__file__).parents[2] / "reports" / "capturespeed" / "reproduce"
ROUTES = REPRODUCE / "capture-routes.tsv"
POWERSHELL = "/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe"


def declared_rows():
    rows = []
    for line in ROUTES.read_text().splitlines():
        if line and not line.startswith("#"):
            rows.append(line.split("\t"))
    return rows


def expected(row):
    capture, state, regex, env, cleanup = row
    return {"regex": regex, "env": "" if env == "-" else env,
            "cleanup": cleanup.replace("{state}", state).split(";")}


def shell_route(capture, state):
    script = (f'script_dir="{REPRODUCE}"; repo_root="{REPRODUCE}"; source "$script_dir/native-common.sh"; '
              f'native_route_lookup "{capture}" "{state}" && '
              'printf "%s\\n%s\\n%s\\n" "$route_regex" "$route_env" "$route_cleanup"')
    result = subprocess.run(["bash", "-c", script], capture_output=True, text=True)
    return result


class RouteTableTests(unittest.TestCase):
    def test_rows_are_well_formed_and_unique(self):
        rows = declared_rows()
        self.assertTrue(all(len(row) == 5 for row in rows))
        keys = [(row[0], row[1]) for row in rows]
        self.assertEqual(len(keys), len(set(keys)))
        for row in rows:
            re.compile(row[2])
        self.assertIn(("glyphsdf", "latin-carved"), keys)
        self.assertIn(("glyphsdf", "analytic-vs-voxel"), keys)

    def test_shell_resolves_every_row(self):
        for row in declared_rows():
            result = shell_route(row[0], row[1])
            self.assertEqual(result.returncode, 0, result.stderr)
            regex, env, cleanup = result.stdout.rstrip("\n").split("\n")
            want = expected(row)
            self.assertEqual((regex, env, cleanup.split(";")), (want["regex"], want["env"], want["cleanup"]))

    def test_shell_rejects_undeclared_state_and_lists_declared(self):
        result = shell_route("glyphsdf", "no-such-state")
        self.assertEqual(result.returncode, 2)
        self.assertIn("latin-carved", result.stderr)
        self.assertEqual(shell_route("nocapture", "x").returncode, 2)

    @unittest.skipUnless(Path(POWERSHELL).exists() and shutil.which("wslpath"), "Windows PowerShell interop unavailable")
    def test_powershell_agrees_with_shell_on_every_row(self):
        win = subprocess.run(["wslpath", "-w", str(REPRODUCE / "native-routes.ps1")],
                             capture_output=True, text=True).stdout.strip()
        rows = declared_rows()
        command = (f". '{win}'; $out = @(); "
                   "foreach ($r in Get-NativeCaptureRoutes) { $x = Get-NativeCaptureRoute -Capture $r.Capture -State $r.State; "
                   "$out += [pscustomobject]@{capture=$r.Capture; state=$r.State; regex=$x.Regex; env=$x.StateEnv; cleanup=@($x.Cleanup)} }; "
                   "ConvertTo-Json -Compress -Depth 4 -InputObject $out")
        result = subprocess.run([POWERSHELL, "-NoLogo", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-Command", command],
                                capture_output=True, text=True, stdin=subprocess.DEVNULL)
        self.assertEqual(result.returncode, 0, result.stderr)
        got = json.loads(result.stdout)
        self.assertEqual(len(got), len(rows))
        for entry, row in zip(got, rows):
            want = expected(row)
            self.assertEqual((entry["capture"], entry["state"]), (row[0], row[1]))
            self.assertEqual((entry["regex"], entry["env"], entry["cleanup"]),
                             (want["regex"], want["env"], want["cleanup"]))


if __name__ == "__main__":
    unittest.main()
