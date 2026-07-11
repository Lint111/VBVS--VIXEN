# Feature Plan Template

Copy this template to `Vixen-Docs/05-Progress/features/{feature-name}.md` when starting a new feature.

---

```markdown
---
tags: [feature, in-progress]
created: {{DATE}}
status: planning | implementing | testing | complete
priority: high | medium | low
hacknplan-task: {{TASK_ID}}
hacknplan-design-element: {{DESIGN_ELEMENT_ID}}
---

# Feature: {{FEATURE_NAME}}

## Overview

**Objective:** One-sentence description of what this feature accomplishes.

**Requester:** User / Self-identified need

**Branch:** `feature/{{branch-name}}`

---

## HacknPlan Integration

### Work Item
- **Task ID:** [#{{TASK_ID}}](https://app.hacknplan.com/p/230809/workitems/{{TASK_ID}})
- **Stage:** Planned → In Progress → Testing → Completed
- **Estimate:** {{ESTIMATED_HOURS}}h
- **Logged:** {{LOGGED_HOURS}}h

### Design Element
- **Element ID:** {{DESIGN_ELEMENT_ID}}
- **Type:** System (9) | Mechanic (10) | Object (12)
- **Name:** {{DESIGN_ELEMENT_NAME}}

### Subtasks
| ID | Title | Estimate | Status |
|----|-------|----------|--------|
| #{{SUBTASK_1}} | Subtask description | Xh | Planned |
| #{{SUBTASK_2}} | Subtask description | Xh | Planned |

---

## Discovery Findings

### Related Code

| File | Relevance | Notes |
|------|-----------|-------|
| `path/to/file.cpp:42` | Direct | Main logic affected |
| `path/to/other.hpp` | Indirect | Interface dependency |

### Related Documentation

- [[RenderGraph-System]] - Relevant section X
- [[Vulkan-Pipeline]] - Pattern to follow

### Affected Subsystems

- [ ] SVO
- [ ] RenderGraph
- [ ] CashSystem
- [ ] VulkanResources
- [ ] Profiler
- [ ] Other: ___

### Complexity Assessment

- **Estimated effort:** Small / Medium / Large
- **Risk level:** Low / Medium / High
- **Breaking changes:** Yes / No

---

## Design Decisions

### Decision 1: {{Decision Title}}

**Context:** Why is this decision needed?

**Options Considered:**
1. **Option A:** Description
   - Pros: ...
   - Cons: ...
2. **Option B:** Description
   - Pros: ...
   - Cons: ...

**Chosen:** Option X

**Rationale:** Why this option was selected.

**User Approved:** Yes / Pending

**Logged to HacknPlan:** [Comment](link)

---

## Implementation Plan

### Phase 1: {{Phase Name}}

- [ ] **Task 1.1:** Description
  - Files: `path/to/file.cpp`
  - Agent: coding-partner
  - Dependencies: None
  - HacknPlan: #{{SUBTASK_ID}}

- [ ] **Task 1.2:** Description
  - Files: `path/to/file.cpp`, `path/to/other.hpp`
  - Agent: intern-army-refactor
  - Dependencies: Task 1.1
  - HacknPlan: #{{SUBTASK_ID}}

### Phase 2: {{Phase Name}}

- [ ] **Task 2.1:** Description
  - Files: ...
  - Agent: ...
  - Dependencies: Phase 1 complete
  - HacknPlan: #{{SUBTASK_ID}}

### Phase 3: Testing

- [ ] **Task 3.1:** Create unit tests for {{component}}
- [ ] **Task 3.2:** Create integration tests for {{flow}}
- [ ] **Task 3.3:** Run full test suite and fix regressions

---

## Test Requirements

### Unit Tests

| Component | Test File | Coverage Target |
|-----------|-----------|-----------------|
| NewClass | `test_new_class.cpp` | 80% |
| ModifiedFunc | `test_existing.cpp` | Update existing |

### Integration Tests

| Flow | Test Description |
|------|------------------|
| End-to-end X | Test that X produces expected Y |

### Edge Cases

- [ ] Empty input
- [ ] Maximum size input
- [ ] Invalid parameters
- [ ] Concurrent access (if applicable)

---

## Acceptance Criteria

- [ ] All unit tests pass
- [ ] All integration tests pass
- [ ] No new warnings introduced
- [ ] Code follows existing patterns
- [ ] Documentation updated
- [ ] No performance regression
- [ ] HacknPlan task completed
- [ ] Design element updated with final notes

---

## Progress Log

### {{DATE}} - Session Started

- HacknPlan task moved to In Progress
- Started discovery phase
- Identified related files
- Created initial plan

**Time logged:** Xh | **Commit:** -

### {{DATE}} - Implementation Progress

- Completed Task 1.1
- Files modified: `path/to/file.cpp:42-67`
- Notes: ...

**Time logged:** Xh | **Commit:** `abc1234` [HP-{{TASK_ID}}]

### {{DATE}} - Review Feedback

- Architecture review: Approved / Changes requested
- QA review: Approved / Changes requested
- Action items: ...

**Time logged:** Xh | **HacknPlan comment:** [Link](url)

---

## Files Changed

| File | Change Type | Lines | Description |
|------|-------------|-------|-------------|
| | | | |

---

## Deferred Work

<!-- TODO:DEFERRED - searchable marker for future work queue -->
Items identified but not addressed in this feature:

- [ ] TODO:DEFERRED Future improvement A (reason deferred)
- [ ] TODO:DEFERRED Related refactor B (out of scope)

<!-- Search all deferred work: grep "TODO:DEFERRED" Vixen-Docs/05-Progress/ -->

---

## Future Extensions

<!-- TODO:EXTENSION - ideas considered but not needed now -->
Potential enhancements considered during design but out of scope for current roadmap:

- [ ] TODO:EXTENSION Extension idea A - Brief description (why not now: complexity/priority/dependencies)
- [ ] TODO:EXTENSION Extension idea B - Brief description (revisit when: condition/milestone)

<!--
Search all extensions: grep "TODO:EXTENSION" Vixen-Docs/05-Progress/
Marker meanings:
  - TODO:DEFERRED = Work needed eventually, just not in this feature
  - TODO:EXTENSION = Nice-to-have, revisit if requirements change
-->

---

## Completion Checklist

- [ ] All tasks complete
- [ ] All tests pass
- [ ] Code reviewed by architecture-critic
- [ ] Code reviewed by coding-partner
- [ ] QA sign-off
- [ ] Documentation updated
- [ ] activeContext.md updated
- [ ] Branch ready for merge
- [ ] HacknPlan task marked complete
- [ ] Design element updated with final implementation notes
- [ ] Time logged to HacknPlan

**Completed:** {{COMPLETION_DATE}}
**Final commit:** `{{COMMIT_HASH}}` [HP-{{TASK_ID}}]
**Total time:** {{TOTAL_HOURS}}h (estimated: {{ESTIMATED_HOURS}}h)
```
