---
name: session-summary
description: Generate comprehensive session handoff documentation. Use when ending a work session to create a detailed summary for the next engineer. Captures changes, errors, decisions, insights, and next steps.
allowed-tools: Read, Grep, Glob, Bash, mcp__obsidian-vault__*, mcp__hacknplan__*, Write, Edit
---

# Session Summary Skill

Generate a detailed session handoff document that enables another engineer to continue work with zero prior context.

## When to Invoke

- User asks for a "summary", "session summary", "handoff", or "wrap up"
- End of a significant work session
- Before switching to a different task/branch
- When user says "I'm done for today"

## Output Location

Create the summary in:
1. `Vixen-Docs/Sessions/YYYY-MM-DD-summary.md` (permanent archive)
2. `memory-bank/activeContext.md` (quick reference for next session)
3. **HacknPlan** - Update work items with session progress

## Required Sections

### 1. Session Overview
```markdown
# Session Summary: YYYY-MM-DD

**Branch:** `current-branch-name`
**Focus:** One-line description of main objective
**Duration context:** What was attempted vs achieved
```

### 2. What Changed (Files Modified)
```markdown
## Files Changed

| File | Change Type | Description |
|------|-------------|-------------|
| `path/to/file.cpp:42-67` | Modified | Added X functionality |
| `path/to/new.hpp` | Created | New header for Y |
| `path/to/removed.cpp` | Deleted | Consolidated into Z |

### Git Status
- Staged: X files
- Unstaged: Y files  
- Untracked: Z files
```

### 3. Errors & Issues (Outstanding)
```markdown
## Outstanding Issues

### Build Errors
- [ ] `libraries/X/src/Y.cpp:123` - Error description
- [ ] `shaders/Z.glsl` - SPIR-V validation failure

### Runtime Issues  
- [ ] Crash in function X under condition Y
- [ ] Performance regression in Z

### Technical Debt
- [ ] TODO added at `file:line` - description
```

### 4. Design Decisions Made
```markdown
## Design Decisions

### Decision 1: [Brief Title]
- **Context:** Why was this decision needed?
- **Choice:** What was decided?
- **Rationale:** Why this approach over alternatives?
- **Trade-offs:** What we gained/sacrificed
- **References:** Links to code, docs, discussions

### Decision 2: [Brief Title]
...
```

### 5. Key Insights & Discoveries
```markdown
## Insights

### Technical Discoveries
- Finding about Vulkan/SVO/RenderGraph behavior
- Performance characteristics observed
- Edge cases discovered

### Codebase Knowledge
- How component X actually works (vs expected)
- Hidden dependencies found
- Patterns to follow/avoid
```

### 6. Next Steps (Prioritized)
```markdown
## Next Steps

### Immediate (Blockers)
1. [ ] Fix X before anything else works
2. [ ] Complete Y to unblock Z

### Short-term (Current Feature)
3. [ ] Implement remaining A functionality
4. [ ] Add tests for B

### Future (Nice to Have)
5. [ ] Refactor C for cleanliness
6. [ ] Optimize D when time permits
```

### 7. Context for Continuation
```markdown
## Continuation Guide

### Where to Start
- Open `file.cpp:line` - this is where work stopped
- Current state: [compiles/runs/crashes/etc]

### Key Files to Understand
1. `path/to/core.cpp` - Main logic for this feature
2. `path/to/types.hpp` - Critical data structures
3. `shaders/relevant.glsl` - GPU-side implementation

### Commands to Run First
```bash
cmake --build build --config Debug --parallel 16
./build/path/to/test.exe --gtest_brief=1
```

### Watch Out For
- Gotcha 1: Don't do X because Y
- Gotcha 2: Z requires special handling
```

## Data Collection Process

When generating summary:

1. **Git Analysis**
   ```bash
   git status
   git diff --stat
   git log --oneline -10
   git branch --show-current
   ```

2. **Read Active Context**
   - `memory-bank/activeContext.md`
   - `memory-bank/progress.md`

3. **HacknPlan Status**
   ```javascript
   // Get in-progress tasks
   mcp__hacknplan__list_work_items({
     projectId: 230809,
     stageId: 2  // In Progress
   })
   ```

4. **Scan for TODOs Added**
   ```bash
   git diff HEAD~5 | grep -E "^\+.*TODO"
   ```

5. **Check Build State**
   ```bash
   cmake --build build --config Debug 2>&1 | tail -20
   ```

6. **Review Conversation Context**
   - Extract decisions from discussion
   - Note error patterns encountered
   - Capture rationale for approaches taken

## Template

```markdown
# Session Summary: {{DATE}}

**Branch:** `{{BRANCH}}`
**Focus:** {{MAIN_OBJECTIVE}}
**Status:** {{BUILD_STATE}} | {{TEST_STATE}}

---

## Files Changed

{{FILE_CHANGES_TABLE}}

---

## Outstanding Issues

{{ERRORS_AND_ISSUES}}

---

## Design Decisions

{{DECISIONS}}

---

## Insights

{{DISCOVERIES}}

---

## Next Steps

{{PRIORITIZED_TODOS}}

---

## Continuation Guide

{{HANDOFF_INSTRUCTIONS}}

---

*Generated: {{TIMESTAMP}}*
*By: Claude Code (session-summary skill)*
```

## Quality Checklist

Before finalizing summary, verify:

- [ ] All modified files listed with line numbers
- [ ] Build/test status accurately reported
- [ ] All blocking errors documented
- [ ] Design decisions include rationale
- [ ] Next steps are actionable (not vague)
- [ ] Continuation guide enables cold start
- [ ] No assumptions about reader's prior knowledge
- [ ] HacknPlan work items updated with session progress

---

## Auto-Commit Step

After generating the session summary, **automatically create a commit** if there are uncommitted changes:

### Commit Process

1. **Check for uncommitted changes**
   ```bash
   git status --porcelain
   ```

2. **If changes exist, stage and commit**
   ```bash
   git add -A
   git commit -m "$(cat <<'EOF'
   docs(session): Session summary YYYY-MM-DD

   ## Session Focus
   {{ONE_LINE_FOCUS}}

   ## Key Changes
   {{BULLET_LIST_OF_MAIN_CHANGES}}

   ## Status
   - Build: {{PASSING/FAILING}}
   - Tests: {{PASSING/FAILING/SKIPPED}}
   - Outstanding issues: {{COUNT}}

   ## Next Steps
   {{TOP_3_PRIORITIES}}

   🤖 Generated with [Claude Code](https://claude.com/claude-code)

   Co-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>
   EOF
   )"
   ```

3. **Report commit hash to user**
   - Show the commit hash created
   - Note if push is needed

### Commit Message Format

The commit message should:
- Use `docs(session):` prefix for session summaries
- Include date in subject line
- Summarize the session focus in body
- List key changes as bullets
- Include build/test status
- List top 3 next steps
- Follow standard commit footer format

### Skip Conditions

Do NOT auto-commit if:
- No uncommitted changes exist
- User explicitly says "don't commit" or "skip commit"
- There are merge conflicts
- Working tree is in detached HEAD state

### Example Commit

```
docs(session): Session summary 2025-12-10

## Session Focus
Implement VoxelAABBCacher for hardware RT pipeline

## Key Changes
- Created VoxelAABBCacher for AABB extraction from scene data
- Refactored AccelerationStructureCacher to use pre-extracted AABBs
- Updated VoxelAABBConverterNode to use new cacher pattern
- Added VOXEL_SCENE_DATA connection in BenchmarkGraphFactory

## Status
- Build: PASSING
- Tests: NOT RUN
- Outstanding issues: 0

## Next Steps
1. Build and verify all nodes compile
2. Run benchmark to test HW RT rendering
3. Profile AABB extraction performance

🤖 Generated with [Claude Code](https://claude.com/claude-code)

Co-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>
```

---

## HacknPlan Sync

After generating the session summary, **sync progress to HacknPlan**:

### 1. Query Active Work Items
```javascript
mcp__hacknplan__list_work_items({
  projectId: 230809,
  stageId: 2  // In Progress
})
```

### 2. Update Each Active Task with Session Progress
```javascript
mcp__hacknplan__add_comment({
  projectId: 230809,
  workItemId: <id>,
  text: `## Session Progress: ${DATE}

**Commits:**
${COMMIT_LIST}

**Files Changed:**
${FILE_LIST}

**Status:** ${STATUS}

**Time Spent:** ~X hours`
})
```

### 3. Move Completed Tasks
If task was completed during session:
```javascript
mcp__hacknplan__update_work_item({
  projectId: 230809,
  workItemId: <id>,
  stageId: 4,  // Completed
  isCompleted: true
})
```

### 4. Create New Tasks for Discovered Work
If session revealed new tasks:
```javascript
mcp__hacknplan__create_work_item({
  projectId: 230809,
  title: "[Component] New task discovered",
  categoryId: 1,  // Programming
  boardId: 649644,
  stageId: 1,  // Planned
  description: "Discovered during session YYYY-MM-DD\n\n## Context\n..."
})
```

### 5. Link Session to HacknPlan
Add to session summary:
```markdown
## HacknPlan Updates

| Work Item | Action | Link |
|-----------|--------|------|
| #123 | Progress comment | [View](https://app.hacknplan.com/p/230809/workitems/123) |
| #124 | Completed | [View](https://app.hacknplan.com/p/230809/workitems/124) |
| #125 | Created (new) | [View](https://app.hacknplan.com/p/230809/workitems/125) |
```

### Git ↔ HacknPlan Cross-Reference

In git commits, reference HacknPlan:
```
feat(component): Description [HP-123]
```

In HacknPlan comments, reference git:
```markdown
**Commits:**
- `abc1234` - feat(component): Description
- `def5678` - fix(component): Bug fix
```

---

## Session Summary Template (Updated)

```markdown
# Session Summary: {{DATE}}

**Branch:** `{{BRANCH}}`
**Focus:** {{MAIN_OBJECTIVE}}
**Status:** {{BUILD_STATE}} | {{TEST_STATE}}
**HacknPlan Tasks:** {{ACTIVE_TASK_IDS}}

---

## Files Changed

{{FILE_CHANGES_TABLE}}

---

## Git Commits This Session

{{COMMIT_LIST_WITH_HP_REFS}}

---

## HacknPlan Updates

{{HACKNPLAN_UPDATES_TABLE}}

---

## Outstanding Issues

{{ERRORS_AND_ISSUES}}

---

## Design Decisions

{{DECISIONS}}

---

## Next Steps

{{PRIORITIZED_TODOS}}

---

## Continuation Guide

{{HANDOFF_INSTRUCTIONS}}

---

*Generated: {{TIMESTAMP}}*
*By: Claude Code (session-summary skill)*
```
