#!/usr/bin/env python3
"""
PermissionRequest notification hook for VIXEN project.

Fires whenever the harness computes a permission decision for a Bash command
that isn't already covered by the allow-list (i.e. a real prompt is about to
be shown to the user). Does not itself allow/deny anything — it only:
  1. Appends the gated command as one JSON-lines record to gated-commands.log
     (timestamp, full command, and a derived "prefix" — the leading binary/
     subcommand shape) so the log can be mined for repeated PATTERNS, not
     just repeated exact strings — e.g. `git status`, `git status --short`,
     and `git status .claude/` all share prefix `git status`, which is what
     you actually want to group by when deciding whether to bless a category.
  2. Injects additionalContext back into the agent's turn, so the agent sees
     in real time that a permission prompt was dispatched and for what
     command — closing the loop where previously a gated tool call just
     looked like "it took a while" with no signal of why.

This hook is read-only with respect to the permission decision itself: it
always exits 0 with no permissionDecision field, so it never overrides the
harness's own allow/ask/deny outcome — it's purely an observability tap.

To mine the log for category candidates once it has some history:
    python3 .claude/hooks/summarize_gated_commands.py
(counts occurrences per derived prefix, sorted most-frequent first).
"""
import json
import sys
import os
from datetime import datetime, timezone

LOG_FILE = os.path.join(os.path.dirname(__file__), 'gated-commands.log')

# How many leading whitespace-separated tokens to use as the "prefix" a
# repeated-pattern scan groups by. 2 tokens covers shapes like "git status",
# "cmd.exe /c", "taskkill /F" — the binary plus its immediate subcommand/flag,
# without over-fitting to full argument lists (which are usually unique per
# invocation and would never show a repeat).
PREFIX_TOKEN_COUNT = 2

def derive_prefix(command):
    tokens = command.strip().split()
    return " ".join(tokens[:PREFIX_TOKEN_COUNT]) if tokens else ""

def main():
    try:
        input_data = json.load(sys.stdin)
        tool_input = input_data.get("tool_input", {})
        command = tool_input.get("command", "")

        record = {
            "timestamp": datetime.now(timezone.utc).isoformat(timespec='seconds'),
            "command": command,
            "prefix": derive_prefix(command),
        }

        try:
            with open(LOG_FILE, 'a') as f:
                f.write(json.dumps(record) + "\n")
        except Exception:
            pass  # logging is best-effort; never block on it

        output = {
            "hookSpecificOutput": {
                "hookEventName": "PermissionRequest",
                "additionalContext": f"[permission-request-notify] A permission prompt was just dispatched for: {command}"
            }
        }
        print(json.dumps(output))
        sys.exit(0)

    except Exception:
        # Fail open — never block a permission request due to a hook error.
        sys.exit(0)

if __name__ == '__main__':
    main()
