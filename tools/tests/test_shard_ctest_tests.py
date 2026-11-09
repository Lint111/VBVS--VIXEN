"""Tests for the queue-facing CTest GPU shard partitioner."""

import json
from pathlib import Path
import subprocess
import tempfile
import unittest


HELPER = Path(__file__).parents[1] / "shard-ctest-tests.py"


class CTestShardTests(unittest.TestCase):
    def test_fixture_cases_stay_in_shard_zero_and_independent_cases_fill_lighter_shards(self):
        tests = [
            {"name": "fixture_setup", "properties": [{"name": "FIXTURES_SETUP", "value": "GPU"}]},
            {"name": "fixture_case", "properties": [{"name": "FIXTURES_REQUIRED", "value": "GPU"}]},
            {"name": "fixture_cleanup", "properties": [{"name": "FIXTURES_CLEANUP", "value": "GPU"}]},
            {"name": "independent_a", "properties": []},
            {"name": "independent_b", "properties": []},
            {"name": "independent_c", "properties": []},
        ]
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory, "tests.json")
            output = Path(directory, "shards")
            source.write_text(json.dumps({"tests": tests}), encoding="utf-8")
            result = subprocess.run(
                ["python3", str(HELPER), "--json", str(source), "--out-dir", str(output), "--shards", "3"],
                capture_output=True, text=True, check=True)
            self.assertEqual(result.stdout.strip(), "3 2 1")
            shard_zero = Path(output, "shard-0.regex").read_text(encoding="utf-8")
            self.assertNotIn("?:", shard_zero)
            self.assertIn("fixture_setup", shard_zero)
            self.assertIn("fixture_case", shard_zero)
            self.assertIn("fixture_cleanup", shard_zero)
            self.assertNotIn("independent_a", shard_zero)
            self.assertNotIn("independent_b", shard_zero)

    def test_costly_cases_spread_across_shards_instead_of_following_listing_order(self):
        def case(name, cost):
            return {"name": name, "properties": [{"name": "COST", "value": cost}]}

        tests = [case("slow_1", 500), case("slow_2", 480), case("slow_3", 470),
                 case("quick_1", 5), case("quick_2", 5), case("quick_3", 5)]
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory, "tests.json")
            output = Path(directory, "shards")
            source.write_text(json.dumps({"tests": tests}), encoding="utf-8")
            subprocess.run(
                ["python3", str(HELPER), "--json", str(source), "--out-dir", str(output), "--shards", "3"],
                capture_output=True, text=True, check=True)
            for index in range(3):
                shard = Path(output, f"shard-{index}.regex").read_text(encoding="utf-8")
                self.assertEqual(shard.count("slow_"), 1, shard)


if __name__ == "__main__":
    unittest.main()
