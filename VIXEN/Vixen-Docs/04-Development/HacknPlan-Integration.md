# HacknPlan Integration Guide

## Project Configuration

| Setting | Value |
|---------|-------|
| **Project ID** | 230809 |
| **Project Name** | Vixen |
| **Default Board** | Sprint 1 (649644) |
| **Cost Metric** | Hours |
| **Hours/Day** | 8 |

---

## Categories (Work Item Types)

| ID | Name | Icon | Use For |
|----|------|------|---------|
| -1 | User Story | list-alt | Epic-level features, user-facing functionality |
| 1 | Programming | code | Implementation tasks, refactoring, optimization |
| 2 | Art | photo | Visual assets, UI design |
| 3 | Design | puzzle-piece | Architecture, system design, planning |
| 4 | Writing | pencil | Documentation, comments, specs |
| 5 | Marketing | line-chart | Release prep, demos |
| 6 | Sound | headphones | Audio assets |
| 7 | Ideas | lightbulb-o | Research, exploration, prototypes |
| 8 | Bug | bug | Defects, issues, crashes |

### Category Selection Guide

```
Feature implementation → Programming (1)
Architecture decision → Design (3)
Bug fix → Bug (8)
Documentation update → Writing (4)
Performance research → Ideas (7) + performance tag
```

---

## Stages (Workflow)

| ID | Name | Status | Description |
|----|------|--------|-------------|
| 1 | Planned | created | Task defined, not started |
| 2 | In Progress | started | Actively being worked on |
| 3 | Testing | started | Implementation complete, validating |
| 4 | Completed | closed | Done and verified |

### Stage Transitions

```
New task → Planned (1)
Start work → In Progress (2)
Code complete → Testing (3)
Tests pass → Completed (4)
```

---

## Importance Levels (Priority)

| ID | Name | Use For |
|----|------|---------|
| 1 | Urgent | Blockers, critical bugs, deadline items |
| 2 | High | Core feature work, important fixes |
| 3 | Normal | Standard tasks (default) |
| 4 | Low | Nice-to-have, future improvements |

---

## Tags

| ID | Name | Color | Use For |
|----|------|-------|---------|
| 1 | vulkan | #AC2226 | Vulkan API, pipeline, synchronization |
| 2 | render-graph | #7585D3 | RenderGraph nodes, connections |
| 3 | svo | #3ECE38 | Sparse Voxel Octree, voxel data |
| 4 | ray-tracing | #FF8800 | RT pipeline, acceleration structures |
| 5 | shader | #DF61C6 | GLSL, SPIR-V, compute shaders |
| 6 | documentation | #5ECDE9 | Docs, comments, specs |
| 7 | refactor | #A87314 | Code cleanup, restructuring |
| 8 | performance | #25ABB1 | Optimization, benchmarking |

### Tag Assignment Guidelines

- Apply **all relevant** tags (multi-tag supported)
- At least one **domain tag** (vulkan, render-graph, svo, ray-tracing, shader)
- Add **documentation** tag if docs are updated
- Add **refactor** tag for structural changes
- Add **performance** tag for optimization work

---

## Design Element Types

| ID | Name | Use For |
|----|------|---------|
| 9 | System | Major engine subsystems (RenderGraph, SVO, Profiler) |
| 10 | Mechanic | Algorithms, techniques (ESVO traversal, beam optimization) |
| 11 | Character | N/A for engine project |
| 12 | Object | Data structures, resources |
| 13 | Folder | Organization/grouping |

### Creating Design Elements

Design elements in HacknPlan should **mirror** Obsidian vault documentation:

| Obsidian Path | HacknPlan Element Type |
|---------------|------------------------|
| `01-Architecture/*.md` | System (9) |
| `03-Research/*.md` | Mechanic (10) |
| `Libraries/*.md` | System (9) |

---

## Work Item Conventions

### Title Format

```
[Component] Brief description

Examples:
[RenderGraph] Add slot type validation
[SVO] Fix ESVO traversal for depth < 23
[Profiler] Implement ZIP packaging for results
[Shader] Optimize voxel ray marching loop
```

### Description Format

```markdown
## Summary
One paragraph explaining the task.

## Requirements
- [ ] Requirement 1
- [ ] Requirement 2

## Related Files
- `libraries/RenderGraph/src/File.cpp:123`
- `shaders/VoxelRT.glsl`

## Vault References
- [[01-Architecture/RenderGraph-System]]
- [[03-Research/ESVO-Algorithm]]

## Acceptance Criteria
- [ ] Build passes
- [ ] Tests pass
- [ ] Documented in vault
```

### Estimated Cost Guidelines

| Task Type | Hours |
|-----------|-------|
| Bug fix (simple) | 0.5 - 1 |
| Bug fix (complex) | 2 - 4 |
| Small feature | 2 - 4 |
| Medium feature | 4 - 8 |
| Large feature | 8 - 16 |
| Architecture change | 16 - 40 |
| Documentation only | 0.5 - 2 |

---

## Subtask Conventions

Parent tasks should be broken into subtasks when:
- Task > 4 hours estimated
- Multiple distinct phases
- Multiple files/components affected

### Subtask Title Format

```
Parent: [RenderGraph] Implement new node type

Subtasks:
- [ ] Define node interface
- [ ] Implement Setup phase
- [ ] Implement Execute phase
- [ ] Add slot connections
- [ ] Write unit tests
- [ ] Update documentation
```

---

## Git Commit ↔ HacknPlan Linking

### Commit Message Format

Include HacknPlan work item ID in commits:

```
feat(render-graph): Add slot validation [HP-123]

Implements validation for slot type compatibility.
See HacknPlan work item #123 for details.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
```

### Comment on Completion

When completing a task via git commit:

```markdown
Completed in commit `abc1234`

Files changed:
- `libraries/RenderGraph/src/Validation.cpp:45-89`
- `libraries/RenderGraph/include/Validation.hpp:12-34`

Tests: PASSING
Build: PASSING
```

---

## Session Workflow

### Starting a Session

1. Query current sprint: `mcp__hacknplan__list_work_items(projectId=230809, stageId=2)`
2. Check what's in progress
3. Update activeContext.md with current focus

### During Development

1. Move task to "In Progress" when starting
2. Add comments with progress updates
3. Link commits to work items

### Ending a Session

1. Update work item stage appropriately
2. Add completion comment with commit refs
3. Update estimated vs actual hours
4. Generate session summary

---

## Cross-Reference Pattern

### Obsidian → HacknPlan

In Obsidian docs, reference HacknPlan items:

```markdown
## Implementation Status

See [HacknPlan #123](https://app.hacknplan.com/p/230809/workitems/123)
```

### HacknPlan → Obsidian

In HacknPlan descriptions, reference vault docs:

```markdown
## Vault References
- Architecture: `Vixen-Docs/01-Architecture/RenderGraph-System.md`
- Research: `Vixen-Docs/03-Research/ESVO-Algorithm.md`
```

---

## API Quick Reference

```javascript
// List in-progress tasks
mcp__hacknplan__list_work_items({ projectId: 230809, stageId: 2 })

// Create task
mcp__hacknplan__create_work_item({
  projectId: 230809,
  title: "[Component] Description",
  categoryId: 1,  // Programming
  boardId: 649644,
  stageId: 1,  // Planned
  importanceLevelId: 3,  // Normal
  estimatedCost: 4  // hours
})

// Move to In Progress
mcp__hacknplan__update_work_item({
  projectId: 230809,
  workItemId: 123,
  stageId: 2
})

// Complete task
mcp__hacknplan__update_work_item({
  projectId: 230809,
  workItemId: 123,
  stageId: 4,
  isCompleted: true
})
```
