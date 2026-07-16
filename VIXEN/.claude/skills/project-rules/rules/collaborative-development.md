# Collaborative Development Rules

<rule id="collaborative-development" reiterate="task-relevant:feature,complex-problem">

## Trigger Conditions

Invoke `collaborative-development` skill when:
- User requests a **new feature** implementation
- User describes a **complex problem** spanning multiple files
- Task requires **architectural decisions**
- User explicitly asks for "collaborative", "iterative", or "group effort" approach

## Pre-Workflow Requirements

Before starting ANY collaborative work:

### Vixen-Docs Documentation

- Check whether a feature doc already exists for this work in `Vixen-Docs/05-Progress/features/`
- If not, create one directly using the template at
  `collaborative-development/templates/feature-plan.md`

## Required Workflow

### Phase Flow
```
Discovery → Plan → Refine → Implement → Review → Test → Complete
    ↓         ↓        ↓          ↓         ↓       ↓        ↓
 Arch doc   User OK  QA review  Progress  Comments  Tests   Log time
```

### Preferred Execution Path

Once a plan is approved, the **default** way to execute it is the `post-brainstorm-context-manager`
skill: it runs the plan as a milestone-chunked, model-tiered, context-budget-aware pipeline — fresh
Sonnet/Haiku workers implement each milestone, Opus validates, progress persists to the plan doc,
and each milestone's worker context is discarded to keep the controller thin. Invoke it right after
plan approval instead of executing milestones inline in the main conversation, unless the task is
small enough that the overhead isn't worth it (e.g. a single-file fix with no distinct milestones).

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
- Update the progress log after each phase, directly in the doc

## Agent Coordination

| Task Type | Agent | Model |
|-----------|-------|-------|
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
1. Create commit describing the change
2. Update the Vixen-Docs feature doc's progress log directly

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

1. Add completion notes to the feature doc (commit hash, files changed)
2. Update the feature doc status to COMPLETE
3. Archive to `Vixen-Docs/05-Progress/completed/`

## Forbidden Shortcuts

- NEVER skip the discovery phase
- NEVER implement before plan approval
- NEVER skip review cycle
- NEVER mark complete with failing tests
- NEVER make architectural choices without user input

</rule>
