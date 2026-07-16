---
name: session-setup
description: Standardized session initialization with memory bank quick check and Vixen-Docs context gathering.
allowed-tools: Read, Grep, Glob
---

# Session Setup Skill

Standardized workflow for initializing a development session.

## When to Invoke

- **Every new conversation** (automatically via project-rules)
- When user says "start session", "new session", "what should I work on?"
- After conversation compression/reset
- When switching between tasks

## Workflow Phases

### Phase 1: Quick State Check (30 seconds)

Read memory bank files for immediate context:

```
Read: memory-bank/activeContext.md
Read: memory-bank/progress.md
```

**Extract:**
- Current focus area
- Recent changes
- Outstanding issues
- Open threads / next steps

---

### Phase 2: Vixen-Docs Context Gathering

For the work at hand, check `Vixen-Docs/` (see `rules/obsidian-first.md` for lookup order):

- `00-Index/Quick-Lookup.md` for a fast topic index
- `01-Architecture/` for relevant system design docs
- `05-Progress/features/` for any in-flight feature doc matching the work

If the user hasn't named specific work yet, summarize what `activeContext.md` and
`progress.md` suggest as the natural next step and ask them to confirm or redirect.

---

### Phase 3: Context Summary

Present consolidated context to the user:

```markdown
## Session Ready

**Focus:** <from activeContext.md>
**Related docs:** [[01-Architecture/ComponentSystem]], [[05-Progress/features/FeaturePlan]]

### Code References
- `libraries/Component/src/Main.cpp:45` - Core implementation
- `libraries/Component/include/Types.hpp` - Type definitions

### Quick Commands
```bash
# Build
cmake --build build --config Debug --parallel 16

# Run related tests
./build/libraries/Component/tests/Debug/test_*.exe --gtest_brief=1
```

**Ready to proceed?**
```

---

## New Work Flow

If the user describes new work with no existing feature doc:

1. Create a feature doc directly in `Vixen-Docs/05-Progress/features/` using the template from
   `collaborative-development/templates/feature-plan.md`
2. Capture initial requirements and plan from the user's description

---

## Integration Points

- **project-rules**: Triggers session-setup on new conversation
- **session.md**: Defines the workflow phases
- **collaborative-development**: Use after session-setup for complex work
- **post-brainstorm-context-manager**: Preferred execution path once a plan is approved
- **session-summary**: Counterpart for session end
