---
name: project-rules
description: VIXEN project rules and behavioral guidelines. Invoke at start of every response to load active rules for the current task context. Contains VIXEN-specific workflow, logging, documentation, build commands, and agent configuration.
allowed-tools: Read, Grep, Glob
---

# VIXEN Project Rules Skill

Invoke this skill to load behavioral rules. Rules are split into sub-files for modularity.

## Rule Loading Protocol

Before EVERY response:
1. Identify task type (coding, building, testing, reviewing, etc.)
2. Load ALWAYS rules: `communication.md`, `engineering.md`, `obsidian-first.md`, `logging.md`, `hacknplan-workflow.md`
3. Load task-relevant rules based on context
4. Display loaded rules at response start

**Note**: `communication.md` and `engineering.md` are loaded from the global `~/.claude/skills/project-rules/rules/`. All other rules are VIXEN-local.

## Sub-Rule Files

| File | Reiterate | Content |
|------|-----------|---------|
| `rules/obsidian-first.md` | always | Vixen-Docs documentation lookup workflow |
| `rules/logging.md` | always | VIXEN Logger system, NODE_LOG macros |
| `rules/hacknplan-workflow.md` | always | Task-driven development workflow |
| `rules/code-review.md` | task:code-review | Review standards, severity levels |
| `rules/workflow.md` | task:coding | Agent selection, file paths |
| `rules/session.md` | situational:new-conversation | Memory bank checklist, HacknPlan init |
| `rules/agents.md` | task:agent-launch | Agent descriptions and delegation |
| `rules/commands.md` | task:building,testing | Build/test commands (cmake) |
| `rules/troubleshooting.md` | situational:build-error | Build and Vulkan runtime fixes |
| `rules/collaborative-development.md` | task:feature,complex-problem | Multi-agent workflow |

## Display Format

```rules
[ACTIVE RULES]
- communication: Maximum signal | No filler phrases
- engineering: No quick fixes | Fix root causes
- obsidian-first: Search Obsidian -> Codebase -> Document
- logging: Log to file | Never console | Structured format
- hacknplan-workflow: Task tracking | Stage updates | Time logging
- [task-relevant rules...]
```
