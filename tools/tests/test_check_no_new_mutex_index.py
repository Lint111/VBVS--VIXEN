"""The mutex checker generates the lock acquisition index; a stale index fails the check (R661.3, T-2939)."""
from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).parents[2] / "VIXEN" / "scripts" / "check-no-new-mutex.sh"
INDEX_RELATIVE = Path("VIXEN") / "Vixen-Docs" / "01-Architecture" / "runtime-lock-index.g.md"
BORROWER = "void Queue::Submit(Device* d) { std::lock_guard g(d->SubmitMutex(q)); }\n"
OWNER = "class Cache {\n  std::mutex m_lock;\n  void Get() { std::lock_guard<std::mutex> g(m_lock); }\n};\n"


class MutexIndexTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.sources = self.root / "VIXEN" / "application"
        (self.root / "VIXEN" / "libraries").mkdir(parents=True)
        self.sources.mkdir(parents=True)
        (self.root / "VIXEN" / "scripts").mkdir()
        (self.sources / "Owner.cpp").write_text(OWNER)

    def run_check(self, *arguments):
        return subprocess.run(["bash", str(SCRIPT), "--root", str(self.root), *arguments],
                              capture_output=True, text=True)

    def fix(self):
        result = self.run_check("--fix")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def index(self):
        return (self.root / INDEX_RELATIVE).read_text()

    def test_new_borrower_appears_in_index(self):
        self.fix()
        self.assertNotIn("Borrower.cpp", self.index())
        (self.sources / "Borrower.cpp").write_text(BORROWER)
        self.fix()
        index = self.index()
        self.assertIn("| `application/Borrower.cpp` | 0 | VK2-borrowed | "
                      "`Queue::Submit` -> `lock_guard(d->SubmitMutex(q))` |", index)
        self.assertIn("`Cache` -> `mutex m_lock`", index)
        self.assertIn("`Cache::Get` -> `lock_guard(m_lock)`", index)
        checked = self.run_check()
        self.assertEqual(checked.returncode, 0, checked.stdout + checked.stderr)

    def test_removed_borrower_disappears_from_index(self):
        (self.sources / "Borrower.cpp").write_text(BORROWER)
        self.fix()
        self.assertIn("Borrower.cpp", self.index())
        (self.sources / "Borrower.cpp").unlink()
        self.fix()
        self.assertNotIn("Borrower.cpp", self.index())
        self.assertIn("Owner.cpp", self.index())
        checked = self.run_check()
        self.assertEqual(checked.returncode, 0, checked.stdout + checked.stderr)

    def test_stale_index_fails_check_and_fix_repairs_it(self):
        self.fix()
        index_path = self.root / INDEX_RELATIVE
        index_path.write_text(index_path.read_text().replace("Cache::Get", "Cache::Renamed"))
        stale = self.run_check()
        self.assertEqual(stale.returncode, 1, stale.stdout + stale.stderr)
        self.assertIn("FAIL: stale acquisition index", stale.stdout)
        self.fix()
        self.assertEqual(self.run_check().returncode, 0)

    def test_missing_index_fails_check(self):
        self.fix()
        (self.root / INDEX_RELATIVE).unlink()
        missing = self.run_check()
        self.assertEqual(missing.returncode, 1, missing.stdout + missing.stderr)
        self.assertIn("FAIL: acquisition index missing", missing.stdout)

    def test_source_change_without_regeneration_fails_check(self):
        self.fix()
        (self.sources / "Owner.cpp").write_text(OWNER.replace("void Get()", "void Fetch()"))
        stale = self.run_check()
        self.assertEqual(stale.returncode, 1, stale.stdout + stale.stderr)
        self.assertIn("FAIL: stale acquisition index", stale.stdout)

    def test_unrelated_edit_does_not_move_the_index(self):
        self.fix()
        before = self.index()
        (self.sources / "Owner.cpp").write_text("// header\n#include <mutex>\n\n\n" + OWNER)
        checked = self.run_check()
        self.assertEqual(checked.returncode, 0, checked.stdout + checked.stderr)
        self.fix()
        self.assertEqual(self.index(), before)
        self.assertNotRegex(before, r"\.(?:cpp|h)[:,]\d")


if __name__ == "__main__":
    unittest.main()
