"""Check that run-capped-scope.sh passes per-invocation env and argv to systemd-run unexpanded."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).parents[1] / "run-capped-scope.sh"
CHILD = 'import os,sys; print(os.environ.get("CAP_VALUE","unset")); sys.exit(int(sys.argv[1]))'


class CappedScopeTests(unittest.TestCase):
    def run_script(self, *args, path=None):
        env = os.environ.copy()
        if path:
            env["PATH"] = f"{path}:{env['PATH']}"
        return subprocess.run(["bash", str(SCRIPT), *args], capture_output=True, text=True, env=env,
                              stdin=subprocess.DEVNULL)

    def test_stub_receives_env_and_command_unexpanded(self):
        with tempfile.TemporaryDirectory() as tmp:
            stub = Path(tmp, "systemd-run")
            stub.write_text('#!/usr/bin/env bash\nprintf "%s\\n" "$@" > "$STUB_RECORD"\n')
            stub.chmod(0o755)
            record = Path(tmp, "args")
            os.environ["STUB_RECORD"] = str(record)
            try:
                result = self.run_script("--memory-max", "2G", "--env", "A=x y $HOME", "--", "cmd", "$X", path=tmp)
            finally:
                del os.environ["STUB_RECORD"]
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(record.read_text().split("\n")[:-1],
                             ["--user", "--scope", "--quiet", "--collect", "-p", "MemoryMax=2G",
                              "--setenv=A=x y $HOME", "--", "cmd", "$X"])

    def test_rejects_bad_env_and_missing_command(self):
        self.assertEqual(self.run_script("--env", "novalue", "--", "true").returncode, 2)
        self.assertEqual(self.run_script("--memory-max", "1G").returncode, 2)

    @unittest.skipUnless(shutil.which("systemd-run") and
                         subprocess.run(["systemd-run", "--user", "--scope", "--quiet", "true"],
                                        capture_output=True, stdin=subprocess.DEVNULL).returncode == 0,
                         "needs a working user systemd-run scope")
    def test_real_scope_keeps_value_and_exit_status(self):
        result = self.run_script("--memory-max", "1G", "--env", "CAP_VALUE=a b $HOME", "--",
                                 "python3", "-c", CHILD, "3")
        self.assertEqual(result.returncode, 3, result.stderr)
        self.assertEqual(result.stdout.strip(), "a b $HOME")


if __name__ == "__main__":
    unittest.main()
