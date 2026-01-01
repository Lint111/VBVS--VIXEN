#!/usr/bin/env python3
"""
Pre-commit gate hook for VIXEN project.

This hook is triggered for ALL Bash commands, but only acts on 'git commit'.

Behavior:
- Non-commit commands: Allow (exit 0 with no output)
- Commit with approval marker: Allow and clean up marker
- Commit without marker: Deny with message to run /pre-commit-review

The pre-commit-review skill creates the marker after review passes,
then performs the commit itself.
"""
import json
import sys
import os

MARKER_FILE = os.path.join(os.path.dirname(__file__), '.pre-commit-approved')

def main():
    try:
        # Read input from stdin (required by hook protocol)
        input_data = json.load(sys.stdin)

        # Get the command being executed
        tool_input = input_data.get("tool_input", {})
        command = tool_input.get("command", "")

        # Only act on git commit commands
        if not command.startswith("git commit"):
            sys.exit(0)  # Allow all non-commit commands

        # Check if pre-commit review approved this
        if os.path.exists(MARKER_FILE):
            # Approval marker exists - allow commit and clean up
            try:
                os.remove(MARKER_FILE)
            except:
                pass
            # Exit 0 with no output = allow
            sys.exit(0)
        else:
            # No approval marker - block and instruct to run skill
            output = {
                "hookSpecificOutput": {
                    "hookEventName": "PreToolUse",
                    "permissionDecision": "deny",
                    "permissionDecisionReason": "[COMMIT BLOCKED] Run /pre-commit-review first. The skill will review your changes and handle the commit if review passes."
                }
            }
            print(json.dumps(output))
            sys.exit(0)

    except Exception as e:
        # On error, allow (fail open) to avoid blocking legitimate commits
        sys.exit(0)

if __name__ == '__main__':
    main()
