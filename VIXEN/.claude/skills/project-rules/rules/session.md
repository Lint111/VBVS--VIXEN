# Session Management Rules

<rule id="session" reiterate="situational:new-conversation">

## Session Start Protocol (MANDATORY)

New conversation? Execute the **Session Initialization Workflow** in order:

### Phase 1: Memory Bank Quick Check

Read these files to understand current state:
- `memory-bank/activeContext.md` - Current focus, recent changes
- `memory-bank/progress.md` - What's done, what's left

### Phase 2: Vixen-Docs Context Gathering

For the task at hand, check `Vixen-Docs/` (see `rules/obsidian-first.md` for lookup order)
for related architecture docs, progress notes, and feature docs. Read what's relevant before
starting work.

### Phase 3: Create/Update Feature Doc (if needed)

For new features or architecture changes:
1. Create or update a feature doc directly in `Vixen-Docs/05-Progress/features/`
2. Add initial requirements and plan

---

## Unit of Work Completion Protocol

After completing a unit of work (feature, bug fix, subtask):

### 1. Create Commit

Standard git commit describing the change.

### 2. Update Vixen-Docs

Directly edit the relevant doc(s):
- Update the feature doc's status/progress log
- Update any affected architecture docs in `01-Architecture/`

---

## Session End Protocol

When ending a session (user says "done", "wrap up", "summarize"):

### 1. Invoke session-summary skill

Generates comprehensive handoff documentation.

### 2. Vixen-Docs Update

- Update feature docs directly
- Create session summary in `Vixen-Docs/05-Progress/` (or `Sessions/` if that's the
  established location)
- Update `memory-bank/activeContext.md`

### 3. Commit Session State

Auto-commit if uncommitted changes exist.

---

## Context Window Management

### Context Preservation

- Summarize frequently to preserve key decisions
- Update Vixen-Docs with discoveries as you go, not just at session end

---

## Full Context (After Reset)

When starting fresh or after conversation compression:

1. `memory-bank/projectbrief.md` - Goals, scope
2. `memory-bank/productContext.md` - Design philosophy
3. `memory-bank/systemPatterns.md` - Architecture patterns
4. `memory-bank/techContext.md` - Tech stack
5. `memory-bank/activeContext.md` - Current focus
6. `memory-bank/progress.md` - Status

---

## Sections to NEVER Delete in activeContext.md

These provide essential long-term context:

- **Week 1 & 1.5+ Success Criteria** - project milestones
- **Known Limitations** - accepted edge cases
- **Reference Sources** - ESVO paths, paper citations
- **Todo List (Active Tasks)** - task tracking across weeks

---

## Quick Reference: Session Lifecycle

```
┌─────────────────────────────────────────────────────────────┐
│                    SESSION START                             │
├─────────────────────────────────────────────────────────────┤
│ 1. Memory Bank Check → Quick state understanding            │
│ 2. Vixen-Docs Check → Related architecture/progress docs    │
│ 3. Feature Doc → Create/update directly if needed           │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│                    ACTIVE WORK                               │
├─────────────────────────────────────────────────────────────┤
│ • Unit complete → Commit + Update Vixen-Docs directly        │
│ • Discoveries → Log to Vixen-Docs as you find them           │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│                    SESSION END                               │
├─────────────────────────────────────────────────────────────┤
│ 1. Session Summary → Handoff documentation                  │
│ 2. Vixen-Docs Update → Feature docs, session archive         │
│ 3. Commit State → Auto-commit if changes exist               │
└─────────────────────────────────────────────────────────────┘
```

</rule>
