# Collaborative Development Rules

<rule id="collaborative-development" reiterate="task-relevant:feature,complex-problem">

## Trigger Conditions

Invoke `collaborative-development` skill when:
- User requests a **new feature** implementation
- User describes a **complex problem** spanning multiple files
- Task requires **architectural decisions**
- User explicitly asks for "collaborative", "iterative", or "group effort" approach

## Pre-Workflow Requirements (MANDATORY)

Before starting ANY collaborative work:

### 1. Verify HacknPlan Task
- Check if matching task exists
- If not, create with full metadata
- Get task ID for tracking

### 2. Ensure Design Element
**Every task MUST have a linked design element.**
- Check if task has `designElementId`
- If not, create design element (System=9, Mechanic=10, Object=12)
- Link to work item

### 3. Vault Documentation
- Create/verify vault doc exists for the design element
- Link vault doc to HacknPlan via glue
- Feature docs go in `Vixen-Docs/05-Progress/features/`

## Required Workflow

### Phase Flow
```
Pre-Setup → Discovery → Plan → Refine → Implement → Review → Test → Complete
     ↓           ↓         ↓        ↓          ↓         ↓       ↓        ↓
  Task/DE    Arch doc   User OK  QA review  HP track  Comments  Tests   Log time
```

### User Approval Checkpoints

**ALWAYS ask user before:**
1. Choosing between alternative architectures
2. Deleting more than ~100 lines
3. Introducing new patterns or dependencies
4. Making breaking API changes
5. Significant refactors

### Documentation Requirements

- Create feature plan in `Vixen-Docs/05-Progress/features/`
- Use template from `collaborative-development/templates/feature-plan.md`
- Link to HacknPlan work item and design element
- Update progress log after each phase
- Update `activeContext.md` on completion

## Agent Coordination

| Task Type | Agent | Model |
|-----------|-------|-------|
| HacknPlan ops | hacknplan-manager | Haiku |
| Obsidian ops | obsidian-manager | Haiku |
| Discovery | architecture-critic | Opus |
| Complex logic | coding-partner | Opus |
| Repetitive changes | intern-army-refactor | Haiku |
| Test creation | test-framework-qa | Opus |
| Bug investigation | bug-hunter | Opus |

### Parallelism Rules

- Max 3 agents simultaneously during implementation
- Sequential only during review phase
- One agent at a time during debugging

## Unit of Work Completion

After completing each subtask:
1. Create commit with `[HP-<id>]` reference
2. Log work session via hacknplan-manager
3. Update vault progress doc
4. Update task stage if needed

## Review Cycle

Every subtask must pass review by:
1. `architecture-critic` - Architectural consistency
2. `coding-partner` - Code quality
3. `test-framework-qa` - Test coverage

**If any reviewer flags issues:**
- Minor: Fix and continue
- Major: Return to implementation phase

## Testing Requirements

After implementation complete:
1. Run existing tests first
2. Create new tests for added functionality
3. If tests fail → fix → re-review → re-test
4. Only complete when all tests pass

## Completion Protocol

1. Log final work session to HacknPlan
2. Add completion comment with commit hash, files changed
3. Update design element with final implementation notes
4. Mark task complete (stageId=4, isCompleted=true)
5. Update vault feature doc status to COMPLETE
6. Archive to `Vixen-Docs/05-Progress/completed/`

## Forbidden Shortcuts

- NEVER skip pre-workflow setup (task + design element)
- NEVER skip discovery phase
- NEVER implement before plan approval
- NEVER skip review cycle
- NEVER mark complete with failing tests
- NEVER make architectural choices without user input
- NEVER leave tasks without design element links

</rule>