"""Test the cold-pipeline driver's process-tree sampler, which must work on Linux and Windows."""
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import time
import unittest

spec = importlib.util.spec_from_file_location("cold", Path(__file__).parents[1] / "measure-cold-pipeline.py")
cold = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cold)


class TreeRssTests(unittest.TestCase):
    def test_own_process_is_resident(self):
        self.assertGreater(cold.tree_rss(os.getpid()), 1 << 20)

    def test_a_child_adds_to_the_tree(self):
        alone = cold.tree_rss(os.getpid())
        child = subprocess.Popen([sys.executable, "-I", "-c", "import time; time.sleep(30)"])
        try:
            deadline = time.time() + 20
            grown = alone
            while time.time() < deadline and grown <= alone:
                time.sleep(0.1)
                grown = cold.tree_rss(os.getpid())
            self.assertGreater(grown, alone)
            self.assertGreater(cold.tree_rss(child.pid), 1 << 20)
        finally:
            child.kill()
            child.wait()

    def test_a_gone_root_is_none(self):
        child = subprocess.Popen([sys.executable, "-I", "-c", "pass"])
        child.wait()
        self.assertIsNone(cold.tree_rss(child.pid))

    def test_parse_bytes(self):
        self.assertEqual(cold.parse_bytes("4G"), 4 << 30)


if __name__ == "__main__":
    unittest.main()
