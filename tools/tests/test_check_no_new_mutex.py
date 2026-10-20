"""--fix classifies a new SubmitMutex() borrower as VK2-borrowed without a hand edit."""
from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).parents[2] / "VIXEN" / "scripts" / "check-no-new-mutex.sh"
BORROWER = "void Submit(Device* d) { std::lock_guard g(d->SubmitMutex(q)); }\n"
OWNER = "struct S { std::mutex m; };\n"


class MutexFixClassificationTests(unittest.TestCase):
    def run_check(self, root, *arguments):
        return subprocess.run(["bash", str(SCRIPT), "--root", str(root), *arguments],
                              capture_output=True, text=True)

    def test_fix_classifies_borrower_and_leaves_owner_unclassified(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            sources = root / "VIXEN" / "application"
            (root / "VIXEN" / "libraries").mkdir(parents=True)
            sources.mkdir(parents=True)
            (root / "VIXEN" / "scripts").mkdir()
            (sources / "Borrower.cpp").write_text(BORROWER)
            (sources / "Owner.cpp").write_text(OWNER)

            fixed = self.run_check(root, "--fix")
            self.assertEqual(fixed.returncode, 0, fixed.stdout + fixed.stderr)
            rows = (root / "VIXEN" / "scripts" / "no-new-mutex.allowlist").read_text()
            self.assertIn("VIXEN/application/Borrower.cpp\t0\tVK2-borrowed\n", rows)
            self.assertIn("VIXEN/application/Owner.cpp\t1\tUNCLASSIFIED\n", rows)

            checked = self.run_check(root)
            self.assertEqual(checked.returncode, 0, checked.stdout + checked.stderr)
            self.assertNotIn("Borrower.cpp", checked.stdout)


if __name__ == "__main__":
    unittest.main()
