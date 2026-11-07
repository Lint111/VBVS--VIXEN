#!/usr/bin/env python3
"""Partition a filtered CTest JSON listing into independent regex shards.

CTest's RESOURCE_LOCK is scoped to one CTest invocation.  The GPU suite therefore
needs separate invocations when the outer queue is the resource arbiter.  Cases that
declare a fixture or dependency stay together in shard zero; the remaining cases are
placed longest-first onto the least loaded shard by their COST property (seconds; an unset cost
counts as one second), so a few multi-minute captures do not pile onto one shard while the others idle.
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


def cost_of(test: dict) -> float:
    try:
        return float(properties(test).get("COST") or 1.0)
    except (TypeError, ValueError):
        return 1.0


def regex_for(names: list[str]) -> str:
    # CTest uses its own regex dialect and does not support Python's
    # non-capturing-group syntax.
    return "^(" + "|".join(re.escape(name) for name in names) + ")$"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--json", required=True, type=Path)
    parser.add_argument("--out-dir", required=True, type=Path)
    parser.add_argument("--shards", required=True, type=int)
    args = parser.parse_args()
    if args.shards < 1:
        parser.error("--shards must be positive")

    listing = json.loads(args.json.read_text(encoding="utf-8"))
    tests = [test for test in listing.get("tests", []) if test.get("name")]
    groups: list[list[str]] = [[] for _ in range(args.shards)]
    loads = [0.0] * args.shards
    independent = []
    for test in tests:
        name = str(test["name"])
        if is_dependent(test):
            groups[0].append(name)
            loads[0] += cost_of(test)
        else:
            independent.append(test)
    for test in sorted(independent, key=cost_of, reverse=True):
        lightest = loads.index(min(loads))
        groups[lightest].append(str(test["name"]))
        loads[lightest] += cost_of(test)

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
