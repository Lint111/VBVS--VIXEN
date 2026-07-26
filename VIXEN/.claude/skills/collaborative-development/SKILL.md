---
name: collaborative-development
description: Multi-agent collaborative development workflow for features and complex problems. Orchestrates planning, implementation, peer review, and HacknPlan/repository-document integration with design element linking.
allowed-tools: Task, Read, Write, Edit, Grep, Glob, Bash, mcp__hacknplan__*
---

# Collaborative Iterative Development

A structured workflow for feature implementation and problem-solving that combines architectural planning, multi-agent execution, peer review, and iterative refinement.

## When to Invoke

- Starting a **new feature** implementation
- Tackling a **complex problem** with multiple components
- Any task requiring **architectural decisions**
- Work that spans **multiple files/subsystems**
- User explicitly requests collaborative/iterative approach

## Core Philosophy

1. **Plan before code** - No implementation without approved design
2. **Multiple perspectives** - Architecture, coding, QA all contribute
3. **Iterative refinement** - Plans and code improve through feedback
4. **User in the loop** - Major decisions require explicit approval
5. **Documentation-driven** - All plans live in `Vixen-Docs/` + HacknPlan
6. **Design element linked** - Every task has architecture documentation

---

## Pre-Workflow: Task & Design Element Setup (MANDATORY)

Before starting any collaborative workflow phase:

### 1. Verify HacknPlan Task Exists

```
Task(hacknplan-manager, "Check/create task for feature:
Feature: <feature name>
1. Search existing tasks for matching work
2. If exists: return task ID and details
3. If not: create new task with full metadata
4. Return: task ID, design element ID (if linked)")
```

### 2. Ensure Design Element Exists

**Every task MUST have a linked design element.** This is the architecture documentation for the feature.

```
Task(hacknplan-manager, "Ensure design element for task #<id>:
1. Check if task has designElementId
2. If yes: return design element details
3. If no:
   - Determine element type (System=9, Mechanic=10, Object=12)
   - Create design element with:
     - Name: <feature/component name>
     - Description: Initial architecture notes
     - Vault reference: Vixen-Docs/<path>
   - Link to work item
4. Return: design element ID, name, vault reference")
```

### 3. Create/link repository documentation

Use `Read`/`Write`/`Edit` directly against `Vixen-Docs/`:

1. Check whether the design-element document exists.
2. If not, create it in the appropriate folder:
   - Architecture: `01-Architecture/`
   - Feature: `05-Progress/features/`
   - Research: `03-Research/`
3. Add the HacknPlan work-item/design-element reference in the document.
4. Return the repository-relative document path.

---

## Workflow Phases

### Phase 1: Discovery & Architecture Review

**Trigger:** User provides feature request or problem statement

**Actions:**
1. **Verify task setup** (see Pre-Workflow above)

2. Launch `architecture-critic` agent to:
   - Explore related files and documentation
   - Understand current architecture context
   - Identify affected subsystems
   - Note potential impact areas

3. Update design element description with findings

4. Create/update feature doc in `Vixen-Docs/05-Progress/features/`

**Output:** Architecture context document with:
- Related existing code (file:line references)
- Relevant documentation links
- Affected subsystems map
- Initial complexity assessment

---

### Phase 2: Plan Creation

**Trigger:** Discovery complete

**Actions:**
1. Draft implementation plan including:
   - High-level approach
   - Breaking changes (if any)
   - Subtasks with dependencies
   - Estimated complexity per subtask
   - Risk areas

2. **USER CHECKPOINT:** Present plan summary, ask for approval

3. Update design element with approved approach

**Decision Points Requiring User Approval:**
- Choice between alternative architectures
- Large deletions (>100 lines)
- New dependencies or libraries
- Breaking API changes
- Significant refactors

---

### Phase 2.5: HacknPlan Task Breakdown

**Trigger:** Plan approved by user

**Actions:**
1. Verify parent work item has:
   - Design element linked
   - `Vixen-Docs` document referenced
   - Full description with requirements

2. Create subtask work items for each plan step:
   ```
   Task(hacknplan-manager, "Create subtasks for #<parent_id>:
   Subtasks:
   - <subtask 1 title> | estimate: <hours>
   - <subtask 2 title> | estimate: <hours>
   Each with:
   - categoryId: 1 (Programming)
   - Link to parent design element
   - Description with documentation refs")
   ```

3. Update the `Vixen-Docs` feature document with the task breakdown

**Output:** HacknPlan work items created with full metadata

---

### Phase 3: Plan Refinement

**Trigger:** Initial plan drafted

**Actions:**
1. Launch `test-framework-qa` agent to review plan for:
   - Testability concerns
   - Missing edge cases
   - Integration test requirements
   - Performance test needs

2. Incorporate QA feedback into plan

3. Update both:
   - Design element description
   - `Vixen-Docs` feature document

**Output:** Final approved plan in:
- HacknPlan design element (architecture source of truth)
- `Vixen-Docs/05-Progress/features/{feature-name}.md` (detailed plan)

---

### Phase 4: Implementation

**Trigger:** HacknPlan tasks created

**Actions:**
1. Move current subtask to "In Progress":
   ```
   Task(hacknplan-manager, "Start subtask #<id>:
   - stageId: 2
   - startDate: today
   - Verify design element link")
   ```

2. Create TodoWrite task list from plan

3. For each subtask:
   - **Simple/repetitive tasks:** Launch `intern-army-refactor` (Haiku)
   - **Core logic tasks:** Launch `coding-partner` (Opus)
   - **Complex algorithms:** Handle directly or with `coding-partner`

4. Subtasks run in parallel where dependencies allow

5. After each subtask completion:
   - Create commit with `[HP-<id>]` reference
   - Log work session to HacknPlan
   - Update the `Vixen-Docs` progress tracker

**Orchestration Rules:**
- Max 3 parallel agents at once
- Wait for dependencies before launching dependent tasks
- Update the `Vixen-Docs` progress tracker after each subtask
- Update HacknPlan task status after each subtask

---

### Phase 5: Peer Review

**Trigger:** Subtask implementation complete

**Actions:**
1. For each completed subtask, launch review cycle:
   - `architecture-critic`: Architectural consistency
   - `coding-partner`: Code quality, patterns
   - `test-framework-qa`: Test coverage

2. Collect feedback from all reviewers

3. If changes needed:
   - Minor: Fix inline
   - Major: Return to Phase 4 for that subtask

4. Log review feedback to HacknPlan comment

**Review Criteria:**
- Follows project patterns
- No introduced technical debt
- Adequate error handling
- Appropriate logging
- Test coverage exists

---

### Phase 6: Testing & Validation

**Trigger:** All subtasks pass peer review

**Actions:**
1. Launch `test-framework-qa` agent to:
   - Create/update unit tests
   - Create/update integration tests
   - Run full test suite

2. **If tests fail:**
   - Analyze failure root cause
   - Return to Phase 4 for fix
   - Re-run Phase 5 review
   - Re-run Phase 6 tests

3. **If tests pass:**
   - Update test metadata
   - Proceed to completion

---

### Phase 7: Completion & Cleanup

**Trigger:** All tests pass

**Actions:**
1. Complete HacknPlan work items:
   ```
   Task(hacknplan-manager, "Complete task #<id>:
   - Add completion comment with:
     - Commit hash
     - Files changed
     - Time spent vs estimated
   - Set stageId: 4, isCompleted: true
   - Log final work session")
   ```

2. Update design element with:
   - Final implementation notes
   - Any deviations from plan
   - Lessons learned

3. Update `Vixen-Docs` documentation:
   - Mark feature doc status: COMPLETE
   - List all files changed
   - Document any deferred work
   - Update architecture docs if design changed

4. Update `memory-bank/activeContext.md`

5. Invoke `session-summary` skill for handoff documentation

6. Launch `project-maintainer` agent to:
   - Clean up temporary files created during development
   - Consolidate scattered notes into canonical docs
   - Archive completed feature plan to `Vixen-Docs/05-Progress/completed/`

---

## Design Element Integration

### Required Fields

Every design element created for a task should have:

| Field | Content |
|-------|---------|
| Name | `[Component] Feature/System Name` |
| Type | System (9), Mechanic (10), or Object (12) |
| Description | Architecture overview + repository-document references |

### Description Template

```markdown
## Overview
One paragraph describing the system/feature.

## Architecture
Key design decisions and patterns used.

## Repository Documentation
- [[01-Architecture/RelatedDoc]]
- [[05-Progress/features/FeaturePlan]]

## Code References
- `libraries/Component/src/Main.cpp`
- `libraries/Component/include/Types.hpp`

## Related Work Items
- #<parent_id> - Main feature
- #<subtask_id> - Subtask 1
```

### Cross-Reference Pattern

```
┌─────────────────┐      ┌─────────────────┐      ┌─────────────────┐
│   HacknPlan     │      │  Design Element │      │   Vixen-Docs    │
│   Work Item     │─────▶│  (Architecture) │◀─────│  Documentation  │
│   #123          │      │  ID: 456        │      │  feature.md     │
└─────────────────┘      └─────────────────┘      └─────────────────┘
        │                        │                        │
        │                        │                        │
        ▼                        ▼                        ▼
┌─────────────────────────────────────────────────────────────────┐
│                  Stable Repository References                  │
│  - Design elements record Vixen-Docs paths                     │
│  - Documents record HacknPlan work-item/design-element IDs     │
│  - Either side remains discoverable without a vault MCP        │
└─────────────────────────────────────────────────────────────────┘
```

---

## Agent Orchestration

See [agent-orchestration.md](agent-orchestration.md) for detailed agent coordination rules.

## Feature Plan Template

See [templates/feature-plan.md](templates/feature-plan.md) for the repository document template.

---

## Quick Reference

| Phase | Primary Agent | HacknPlan Action | Documentation Action |
|-------|---------------|------------------|--------------|
| Pre | hacknplan-manager | Create/verify task + design element | Create/link doc |
| 1. Discovery | `architecture-critic` | Update design element | Create feature doc |
| 2. Plan | Main Claude | Add plan to description | Update feature doc |
| 2.5. Tasks | hacknplan-manager | Create subtasks | Update task breakdown |
| 3. Refine | `test-framework-qa` | Update design element | Add test requirements |
| 4. Implement | `coding-partner` / `intern-army-refactor` | Move to In Progress | Log progress |
| 5. Review | All agents | Add review comments | Document feedback |
| 6. Test | `test-framework-qa` | Update status | Add test results |
| 7. Complete | `session-summary` + `project-maintainer` | Complete + log time | Archive feature doc |

---

## User Approval Checkpoints

**MUST get user approval before:**

1. **Architecture decisions:** "I'm considering approach A vs B. A offers X but requires Y. B is simpler but Z. Which do you prefer?"

2. **Large deletions:** "This refactor would delete ~200 lines from X. The functionality moves to Y. Proceed?"

3. **Breaking changes:** "This changes the API signature of X. Callers A, B, C need updates. Approve?"

4. **New patterns:** "I'd like to introduce pattern X for this feature. It would become the standard for similar cases. Agree?"

---

## Failure Handling

### Build Failure
1. Capture error output
2. Launch `bug-hunter` if non-obvious
3. Fix and re-run Phase 5-6
4. Log failure and resolution to HacknPlan

### Test Failure
1. Analyze with `test-framework-qa`
2. Determine: test bug vs code bug
3. Fix appropriate layer
4. Re-run from Phase 5

### Review Rejection
1. Document rejection reason in HacknPlan comment
2. Return to Phase 4 with specific fixes
3. Re-submit for review

---

## Integration with Other Skills

- **project-rules**: Load at skill start
- **session.md**: Follow session start/end protocols
- **hacknplan-workflow**: Delegate all HacknPlan ops to agents
- **session-summary**: Invoke at Phase 7 for handoff documentation
- **project-maintainer**: Invoke at Phase 7 for cleanup and doc consolidation
- **red-green-test-cycle**: Use in Phase 6 for systematic test fixing
- **gpu-shader-debug**: Invoke if shader work involved

---

## Extended Context Usage

This workflow benefits from extended context windows:

- **Phase 1**: Load full architecture docs for discovery
- **Phase 4**: Keep the full design element + `Vixen-Docs` document loaded
- **Phase 5**: Load all review feedback simultaneously

Use context judiciously—summarize findings to HacknPlan and `Vixen-Docs` to preserve them across
sessions.
