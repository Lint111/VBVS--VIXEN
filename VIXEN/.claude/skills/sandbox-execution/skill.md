
# Sandbox Execution Skill

Governance rules for code-executor-MCP sandbox interactions. Ensures scripts are traceable, safe, and produce reusable patterns.

## When to Invoke

Load this skill when:
- Writing scripts for sandbox execution
- Reviewing sandbox execution results
- Promoting script patterns to library

## Core Principles

| Principle | Description |
|-----------|-------------|
| **Transparency** | Every script archived with context and results |
| **Safety** | Destructive actions require explicit user confirmation |
| **Reusability** | Successful patterns promoted to library |
| **Cleanup** | Archives cleared at task milestones, not accumulated forever |

## Directory Structure

```
temp/sandbox/
├── active/                    # Current session scripts (TRANSIENT)
│   └── {timestamp}-{purpose}/
│       ├── script.ts          # Executed code
│       ├── context.json       # Intent, params, triggering task
│       └── result.json        # Output, errors, duration
└── .cleanup-marker            # Signals active session

memory-bank/sandbox-patterns/  # Project-specific patterns (PERMANENT)
└── {category}/
    └── {pattern-name}.ts

~/.claude/sandbox-patterns/    # Cross-project patterns (PERMANENT)
└── {category}/
    └── {pattern-name}.ts
```

## Rule Files

| File | Purpose |
|------|---------|
| `rules/permissions.md` | Destructive action gates, confirmation flow |
| `rules/archival.md` | Script logging requirements, metadata schema |
| `rules/debugging.md` | Logging, error handling, output standards |
| `rules/python-execution.md` | Python sandbox patterns, MCP tool calls, bad examples |

## Workflow

```
1. Agent needs multi-tool operation
   ↓
2. Writes script following templates/script-header.ts
   ↓
3. Script archived to temp/sandbox/active/{timestamp}-{purpose}/
   ↓
4. Sandbox executes, pausing for destructive confirmations
   ↓
5. Results logged to result.json
   ↓
6. Task milestone reached → user triggers cleanup
   ↓
7. Useful patterns → promoted to permanent pattern library
```

## Quick Reference

- **Active scripts**: `temp/sandbox/active/` (transient, cleared on cleanup)
- **Project patterns**: `memory-bank/sandbox-patterns/` (permanent)
- **User patterns**: `~/.claude/sandbox-patterns/` (permanent, cross-project)
- **Cleanup trigger**: Manual, at task completion (only clears temp/sandbox/active/)
- **Destructive ops**: Require double-confirm (sandbox pause → user approval)
