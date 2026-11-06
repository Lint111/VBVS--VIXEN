#!/usr/bin/env python3
"""Partition a filtered CTest JSON listing into independent regex shards.

CTest's RESOURCE_LOCK is scoped to one CTest invocation.  The GPU suite therefore
needs separate invocations when the outer queue is the resource arbiter.  Cases that
declare a fixture or dependency stay together in shard zero; the remaining cases are
spread by recorded cost (longest first onto the least-loaded shard) so one unusually long
capture does not leave the other shards idle.  Costs come from CTest's own
Testing/Temporary/CTestCostData.txt (average seconds per test, written by earlier runs of this
build tree); tests without a recorded cost count as the median cost, and with no cost data at
all the order degrades to the previous round-robin.  Only the partition changes, never which
tests run or how.
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


def properties(test: dict) -> dict[str, object]:
    return {entry.get("name", ""): entry.get("value") for entry in test.get("properties", [])}


def is_dependent(test: dict) -> bool:
    props = properties(test)
    return any(props.get(name) for name in (
        "DEPENDS", "FIXTURES_REQUIRED", "FIXTURES_SETUP", "FIXTURES_CLEANUP"
    ))


def load_costs(path: Path | None) -> dict[str, float]:
    """name -> average seconds from a CTestCostData.txt ("name runs avg" per line)."""
    costs: dict[str, float] = {}
    if path is None or not path.is_file():
        return costs
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.strip() == "---":
            break
        parts = line.rsplit(" ", 2)
        if len(parts) != 3:
            continue
        try:
            costs[parts[0]] = float(parts[2])
        except ValueError:
            continue
    return costs


def regex_for(names: list[str]) -> str:
    # CTest uses its own regex dialect and does not support Python's
    # non-capturing-group syntax.
    return "^(" + "|".join(re.escape(name) for name in names) + ")$"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--json", required=True, type=Path)
    parser.add_argument("--out-dir", required=True, type=Path)
    parser.add_argument("--shards", required=True, type=int)
    parser.add_argument("--cost-data", type=Path, default=None,
                        help="CTestCostData.txt used to balance shards by recorded test time")
    args = parser.parse_args()
    if args.shards < 1:
        parser.error("--shards must be positive")

    listing = json.loads(args.json.read_text(encoding="utf-8"))
    tests = [test for test in listing.get("tests", []) if test.get("name")]
    groups: list[list[str]] = [[] for _ in range(args.shards)]
    independent = []
    dependent_names = []
    for test in tests:
        name = str(test["name"])
        if is_dependent(test):
            groups[0].append(name)
            dependent_names.append(name)
        else:
            independent.append(name)
    costs = load_costs(args.cost_data)
    known = sorted(costs[name] for name in independent if name in costs)
    fallback = known[len(known) // 2] if known else 1.0
    loads = [0.0] * args.shards
    for name in dependent_names:
        loads[0] += costs.get(name, fallback)
    if known:
        # Longest processing time first; ties keep listing order (sort is stable).
        ordered = sorted(independent, key=lambda n: -costs.get(n, fallback))
        for name in ordered:
            target = min(range(args.shards), key=lambda i: loads[i])
            groups[target].append(name)
            loads[target] += costs.get(name, fallback)
    else:
        for index, name in enumerate(independent):
            groups[index % args.shards].append(name)

    args.out_dir.mkdir(parents=True, exist_ok=True)
    for index, names in enumerate(groups):
        (args.out_dir / f"shard-{index}.regex").write_text(
            regex_for(names) if names else "^$", encoding="utf-8"
        )
        (args.out_dir / f"shard-{index}.count").write_text(f"{len(names)}\n", encoding="utf-8")
    print(" ".join(str(len(names)) for names in groups))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
