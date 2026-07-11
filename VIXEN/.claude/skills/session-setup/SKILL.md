---
name: session-setup
description: Standardized session initialization with HacknPlan sprint check, task selection, design element verification, and Obsidian context gathering.
allowed-tools: Task, Read, mcp__hacknplan__*, mcp__obsidian-vault__*, mcp__hacknplan-obsidian-glue__*
---

# Session Setup Skill

Standardized workflow for initializing a development session with full HacknPlan and Obsidian integration.

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
- Active task IDs

---

### Phase 2: HacknPlan Sprint Check (via agent)

```
Task(hacknplan-manager, model=haiku, "Session initialization:
1. Get current board details (ID: 649722 - Sprint 2)
2. List all work items on current board
3. Identify:
   - In Progress tasks (stageId=2)
   - Planned tasks assigned to me (stageId=1)
   - High priority unstarted tasks
4. Return structured summary:
   - Sprint: name, dates, status
   - Active: [id, title, stage, estimate, designElementId]
   - Queued: [id, title, priority, estimate]
   - Recommended next: [id, title, reason]")
```

**Expected Output:**
```
Sprint: Sprint 2 - Data Collection Polish
Status: Active, X tasks remaining

IN PROGRESS:
- #123 [Profiler] Add sanity checker | 4h | DE: 456

PLANNED (Assigned):
- #124 [Benchmark] ZIP packaging | 2h
- #125 [Metrics] Export improvements | 3h

RECOMMENDED NEXT:
#124 - Highest priority, no dependencies
```

---

### Phase 3: Task Selection

Present options to user:

```markdown
## Current Sprint Tasks

### In Progress
| ID | Title | Estimate | Design Element |
|----|-------|----------|----------------|
| #123 | [Profiler] Sanity checker | 4h | [DE-456] |

### Ready to Start
| ID | Title | Priority | Estimate |
|----|-------|----------|----------|
| #124 | [Benchmark] ZIP packaging | High | 2h |
| #125 | [Metrics] Export improvements | Normal | 3h |

**Which task would you like to work on?** (Enter ID or describe new work)
```

---

### Phase 4: Task Activation

When user selects a task:

```
Task(hacknplan-manager, model=haiku, "Activate task #<id>:
1. Update:
   - stageId: 2 (In Progress)
   - startDate: 2025-12-14 (today)
2. Verify designElementId exists
3. If no design element:
   - Return 'NEEDS_DESIGN_ELEMENT'
   - Include task details for element creation
4. Return:
   - Task details (title, description, estimate)
   - Design element details (if exists)
   - Related vault docs (from description)")
```

**If NEEDS_DESIGN_ELEMENT:**
```
Task(hacknplan-manager, model=haiku, "Create design element for task #<id>:
1. Determine type from task:
   - System (9): Major component work
   - Mechanic (10): Algorithm/technique
   - Object (12): Data structure
2. Create element:
   - Name: Extract from task title
   - Description: Initial architecture from task description
3. Link to work item
4. Return: element ID, name")
```

---

### Phase 5: Context Gathering

For the activated task:

```
Task(obsidian-manager, model=haiku, "Gather context for task #<id>:
1. Design element: <id from Phase 4>
2. Actions:
   - Get design element full description
   - Find vault docs referenced in description
   - Check glue mapping for related docs
   - Extract code file references
3. Return:
   - Design element content
   - Vault doc paths (existing)
   - Code file paths mentioned
   - Missing docs (need creation)")
```

**If vault doc missing:**
```
Task(obsidian-manager, model=haiku, "Create vault doc for design element:
- Element: <id>, <name>
- Type: <System|Mechanic|Object>
- Create in:
  - System → 01-Architecture/
  - Mechanic → 03-Research/
  - Feature → 05-Progress/features/
- Include:
  - HacknPlan link
  - Initial structure from design element
  - Code references
- Update glue mapping")
```

---

### Phase 6: Context Summary

Present consolidated context to user:

```markdown
## Session Ready: Task #<id>

**Task:** [Component] Description
**Estimate:** Xh | **Priority:** High
**Design Element:** [DE-456](link)

### Architecture Context
<summary from design element>

### Related Documentation
- [[01-Architecture/ComponentSystem]] - Main architecture doc
- [[05-Progress/features/FeaturePlan]] - Implementation plan

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

## New Task Creation Flow

If user describes work not matching existing tasks:

### Step 1: Create Work Item
```
Task(hacknplan-manager, model=haiku, "Create new task:
- Title: [Component] <description>
- Category: <1=Programming, 3=Design, 8=Bug>
- Board: 649722 (current sprint)
- Stage: 2 (In Progress - starting now)
- Priority: <1-4 based on user input>
- Estimate: <hours>
- Start date: today
- Description: <structured description>")
```

### Step 2: Create Design Element
```
Task(hacknplan-manager, model=haiku, "Create design element:
- Type: <9|10|12>
- Name: <component/feature name>
- Description: Initial architecture
- Link to new work item")
```

### Step 3: Create Vault Doc
```
Task(obsidian-manager, model=haiku, "Create vault doc:
- Path: 05-Progress/features/<name>.md
- Template: feature-plan.md
- Include:
  - HacknPlan work item link
  - Design element link
  - Initial requirements from user")
```

### Step 4: Update Glue
```
Task(obsidian-manager, model=haiku, "Update glue mapping:
- New vault doc: <path>
- Design element: <id>
- Ensure cross-references work")
```

---

## Quick Reference

### Agent Delegation

| Operation | Agent | Model |
|-----------|-------|-------|
| Sprint check | hacknplan-manager | Haiku |
| Task activation | hacknplan-manager | Haiku |
| Design element creation | hacknplan-manager | Haiku |
| Vault doc operations | obsidian-manager | Haiku |
| Glue updates | obsidian-manager | Haiku |

### Key IDs

| Resource | ID |
|----------|-----|
| Project | 230809 |
| Current Board | 649722 |
| Programming Category | 1 |
| Design Category | 3 |
| Bug Category | 8 |
| System Element Type | 9 |
| Mechanic Element Type | 10 |
| Object Element Type | 12 |

### Stage IDs

| Stage | ID |
|-------|-----|
| Planned | 1 |
| In Progress | 2 |
| Testing | 3 |
| Completed | 4 |

---

## Output Format

Session setup should conclude with:

```markdown
## Session Initialized

**Task:** #<id> - [Component] Description
**Sprint:** Sprint 2 - Data Collection Polish
**Estimate:** Xh

**Context Loaded:**
- Design Element: DE-<id>
- Vault Docs: X files
- Code Refs: Y files

**Ready to work. Use `collaborative-development` skill for complex features.**
```

---

## Error Handling

### No Tasks in Sprint
```
No tasks found in current sprint.

Options:
1. Create new task (describe the work)
2. Check backlog for unassigned tasks
3. Review completed tasks for follow-up work
```

### Design Element Creation Failed
```
Could not create design element.

Manual steps:
1. Go to HacknPlan Design tab
2. Create element type: <recommended>
3. Link to work item #<id>
```

### Vault Doc Creation Failed
```
Could not create vault doc.

Manual steps:
1. Create file: Vixen-Docs/05-Progress/features/<name>.md
2. Use template from collaborative-development skill
3. Add HacknPlan cross-reference
```

---

## Integration Points

- **project-rules**: Triggers session-setup on new conversation
- **session.md**: Defines the workflow phases
- **collaborative-development**: Use after session-setup for complex work
- **hacknplan-workflow**: Defines task metadata requirements
- **session-summary**: Counterpart for session end
