#!/usr/bin/env python3
"""
Summarize gated-commands.log for permission-widening decisions.

Groups logged permission prompts by their derived "prefix" (see
permission-request-notify.py) and prints the most frequent ones first, with
example full commands for each — so you can eyeball whether a repeated
prefix is safe to bless as a category (Bash(<prefix>:*)) versus something
that should stay gated per-invocation (e.g. it's destructive, or its
arguments vary in a way that actually matters for safety).

Usage:
    python3 .claude/hooks/summarize_gated_commands.py [--min-count N] [--examples N]
"""
import json
import os
import sys
import argparse
from collections import defaultdict

LOG_FILE = os.path.join(os.path.dirname(__file__), 'gated-commands.log')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--min-count', type=int, default=2,
                         help='Only show prefixes seen at least this many times (default: 2)')
    parser.add_argument('--examples', type=int, default=3,
                         help='Max example full commands to show per prefix (default: 3)')
    args = parser.parse_args()

    if not os.path.exists(LOG_FILE):
        print(f"No log file at {LOG_FILE} yet — nothing gated since this hook was installed.")
        return

    by_prefix = defaultdict(list)
    malformed = 0
    with open(LOG_FILE) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                rec = json.loads(line)
                by_prefix[rec.get("prefix", "")].append(rec)
            except json.JSONDecodeError:
                malformed += 1

    if not by_prefix:
        print("Log exists but has no valid records yet.")
        return

    ranked = sorted(by_prefix.items(), key=lambda kv: len(kv[1]), reverse=True)

    print(f"{'COUNT':>6}  PREFIX")
    print("-" * 60)
    for prefix, records in ranked:
        if len(records) < args.min_count:
            continue
        print(f"{len(records):>6}  {prefix!r}")
        for rec in records[:args.examples]:
            print(f"          {rec['timestamp']}  {rec['command']}")
        if len(records) > args.examples:
            print(f"          ... and {len(records) - args.examples} more")
        print()

    total = sum(len(r) for r in by_prefix.values())
    shown = sum(len(r) for p, r in by_prefix.items() if len(r) >= args.min_count)
    print(f"Total gated commands logged: {total} ({len(by_prefix)} distinct prefixes)")
    print(f"Shown above (count >= {args.min_count}): {shown}")
    if malformed:
        print(f"Skipped {malformed} malformed log line(s).")

if __name__ == '__main__':
    main()
