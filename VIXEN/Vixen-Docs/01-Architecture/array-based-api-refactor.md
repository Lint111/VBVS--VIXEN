---
tags: [architecture, api-design, hacknplan, tool-optimization]
created: 2025-12-21
status: planning
hacknplan: 142
---

# Array-Based API Refactor: HacknPlan MCP Tool Consolidation

## Executive Summary

This document outlines a comprehensive API redesign for the HacknPlan MCP Server that reduces the tool surface area by 74% (from 117 to 30 tools) while improving usability and reducing context overhead by 69%.

**Core Innovation:** Unify all singular and batch operations into array-based tools where every operation accepts an array. Single-item calls use a 1-item array, batch operations use N-item arrays. The pattern scales transparently.

**Status:** Planning phase - Approved for implementation  
**HacknPlan Reference:** [Task #142](https://app.hacknplan.com/p/230809/workitems/142)  
**Related Analysis:** [[../../hacknplan-mcp/TOOL-ANALYSIS.md|TOOL-ANALYSIS.md]] | [[../../hacknplan-mcp/IMPLEMENTATION-SUMMARY.md|IMPLEMENTATION-SUMMARY.md]]

---

## Current State Analysis

### Tool Proliferation Problem

The HacknPlan MCP server currently exposes **117 tools** across 14 domains. This creates several problems:

1. **Cognitive Overload:** Users must choose between singular and batch variants
   - `create_work_item()` vs `batch_create_work_items()`
   - `add_comment()` vs `batch_add_comments()`
   - `assign_work_item()` vs `batch_assign_tags()`

2. **API Schema Bloat:** Token usage problem
   - Each singular tool = ~20 tokens in MCP schema
   - Duplicate batch variant = ~30 additional tokens
   - 117 tools × ~50 tokens = ~5,850 tokens per MCP invocation
   - 69% of tokens used just to describe tool options

3. **Redundancy:** 47 tools are mergeable through array-based pattern
   - Work items: 11 tools consolidate to 1 enhanced tool
   - Comments: 3 tools consolidate to 1 array tool
   - Boards: 4 tools consolidate to 1 array tool
   - Projects: 6 tools consolidate to 2 array tools

4. **Inconsistent Patterns:** Different tools use different batch patterns
   - Some return `{ items: [...] }`
   - Some return `{ successful, failed, errors }`
   - Some don't support batching at all

### Tool Hot/Cold Analysis

From comprehensive usage analysis (see [[../../hacknplan-mcp/TOOL-ANALYSIS.md]]):

- **Hot Path (30 tools):** Used frequently in real workflows
  - Work items CRUD (create, read, update, delete, list)
  - Queries (my tasks, blockers, sprint progress)
  - Workflow shortcuts (start_task, complete_task)
  - Metadata (stages, tags, categories, users)
  
- **Cold Path (87 tools):** Used rarely or never
  - Admin operations (create/update/delete projects)
  - Attachment management (upload, download, delete)
  - Edge cases (dependencies, design elements)
  - Deprecated variants (atomic operations, old batch patterns)

---

## Proposed Architecture

### Core Principle: Array-Based Unified Pattern

Every operation accepts an array of items. The pattern is transparent:

```typescript
// Single item (same interface as batch)
create_work_items({
  items: [{ title: "Task 1", categoryId: 1 }]
})

// Batch operation (same interface as single)
create_work_items({
  items: [
    { title: "Task 1", categoryId: 1 },
    { title: "Task 2", categoryId: 1 },
    { title: "Task 3", categoryId: 1 }
  ]
})
```

### Unified Response Format

All array-based operations return consistent structure:

```typescript
{
  items: T[],                          // Result array (matches input length)
  total: number,                       // Total items processed
  successful: number,                  // Count of successful operations
  failed: number,                      // Count of failed operations
  errors: Array<{                      // Per-item errors with context
    index: number,                     // Position in input array
    error: string,                     // Error message
    details?: any                      // Optional error details
  }>
}
```

### Architecture: Internal Operations Module

Non-exposed internal module for reusable logic:

```
src/internal/
├── work-item-ops.ts          # createSingleWorkItem, updateSingleWorkItem, deleteSingleWorkItem
├── comment-ops.ts            # createSingleComment, updateSingleComment, deleteSingleComment
├── board-ops.ts              # createSingleBoard, updateSingleBoard
├── project-ops.ts            # createSingleProject, updateSingleProject
└── batch-wrapper.ts          # Shared utilities for array processing and error handling
```

Each internal operation exports:
- Single-item handler (e.g., `createSingleWorkItem`)
- Reusable validation and error handling
- Partial success tracking

### Public API: Slim Mode (30 Tools)

#### Work Items Domain (6 tools)
1. **`create_work_items`** - Create 1-N work items in single call
   - Replaces: `create_work_item`, `batch_create_work_items`
   - Input: `{ items: [{ title, categoryId, ... }] }`
   - Parallel processing with per-item error tracking

2. **`update_work_items`** - Update 1-N work items with unified setter
   - Replaces: `update_work_item`, `batch_update_work_items`, `assign_work_item`, `unassign_work_item`, `set_work_item_tags`, `add_work_item_tag`, `remove_work_item_tag`, `set_work_item_parent`, `set_work_item_design_element`, `assign_work_item_to_me`
   - Input: `{ items: [{ workItemId, assignedUserIds?, tagIds?, parentStoryId?, ... }] }`
   - Auto-creates tags if given string names instead of IDs

3. **`delete_work_items`** - Delete 1-N work items with confirmation
   - Replaces: `delete_work_item`, `batch_delete_work_items`
   - Built-in recovery cache (survives server restarts)
   - 2-step confirmation flow: `batch_delete_work_items` + `confirm_deletion`

4. **`list_work_items`** - List work items with filtering and pagination
   - Replaces: `list_work_items`, `list_work_items_slim`
   - Auto-handles server-side pagination (loops until all results)
   - Supports filtering by board, stage, category, tag, user, milestone

5. **`get_work_item`** - Get single work item details
   - Cached responses (5-minute TTL)
   - Returns full work item with dependencies

6. **`search_work_items`** - Multi-criteria search with advanced filters
   - Replaces basic filtering in list_work_items
   - Supports complex queries (title contains, estimate range, completion status)

#### Query/Analytics Domain (4 tools)
7. **`get_my_current_tasks`** - Get tasks assigned to current user with statistics
   - Enhanced with stage distribution, priority breakdown
   - Auto-fetches all pages (no pagination needed)

8. **`get_blockers`** - Analyze blocking dependencies for work item
   - Returns: work items blocking progress
   - Supports transitive dependency analysis
   - Falls back to description-based markers if API doesn't support

9. **`get_sprint_progress`** - Get sprint status with burndown and dependency graph
   - Burndown chart data
   - Dependency graph (blocking relationships)
   - Hour tracking across sprint
   - Stage distribution

10. **`get_sprint_summary`** - Lightweight sprint overview with task counts
    - Task counts by stage
    - Completion percentage
    - Time-based metrics

#### Workflow Shortcuts Domain (3 tools)
11. **`start_task`** - Move task to "In Progress" with time tracking start
    - Accepts task ID or title substring
    - Auto-records start timestamp in time-tracking cache
    - Optional comment support

12. **`complete_task`** - Mark task complete with auto-logging
    - Auto-calculates elapsed time if started with `start_task`
    - Optional manual time override
    - Optional completion comment
    - Atomically: marks complete + logs time + adds comment

13. **`create_subtask`** - Create and link subtask to parent in one call
    - Inherits board from parent if not specified
    - Replaces: `create_work_item` + `set_work_item_parent` sequence

#### Work Logging Domain (1 tool)
14. **`log_work_sessions`** - Log 1-N time entries in single call
    - Replaces: `log_work_session`, `batch_log_work`
    - Input: `{ items: [{ workItemId, hours, description?, date? }] }`
    - Supports backdating (past sessions)

#### Metadata Domain (5 tools)
15. **`list_stages`** - List workflow stages (cached)
16. **`list_categories`** - List work item categories (cached)
17. **`list_tags`** - List project tags (cached)
18. **`list_importance_levels`** - List priority levels (cached)
19. **`list_users`** - List project team members (cached)

#### Projects/Boards Domain (4 tools)
20. **`list_projects`** - List accessible projects (with pagination)
21. **`list_boards`** - List sprints/boards for project
22. **`get_board`** - Get single board details with metrics
23. **`create_boards`** - Create 1-N boards in single call
    - Replaces: `create_board`, `batch_create_boards`

#### Comments Domain (2 tools)
24. **`add_comments`** - Add 1-N comments to work items
    - Replaces: `add_comment`, `batch_add_comments`
    - Input: `{ items: [{ workItemId, text, ... }] }`
    - Markdown support

25. **`list_comments`** - List comments on work item with pagination

#### Design Elements Domain (3 tools)
26. **`list_design_elements`** - List game design docs with filtering
27. **`create_design_elements`** - Create 1-N design elements
    - Replaces: `create_design_element`, `batch_create_design_elements`
28. **`get_design_element`** - Get single design element details

#### Deletion Safety Domain (2 tools)
29. **`delete_work_items`** - 2-step delete with recovery cache
    - Uses recovery cache for 30-day restoration window
    - Supports 2-step confirmation flow

30. **`recover_deleted_items`** - Restore deleted work items from cache
    - Creates new items (cannot restore original IDs)
    - Validates cache state

**Total: 30 tools (74% reduction from 117)**

---

## Implementation Strategy

### Phase 1: Extract Internal Operations (2-3 hours)

Create non-exposed internal module with reusable logic:

- [ ] Create `src/internal/work-item-ops.ts`
  - Extract `createSingleWorkItem()` from current `create_work_item` handler
  - Extract `updateSingleWorkItem()` from current `update_work_item` handler
  - Extract `deleteSingleWorkItem()` from current `delete_work_item` handler
  - Maintain current validation and error handling

- [ ] Create `src/internal/batch-wrapper.ts`
  - Generic batch processing utility
  - Parallel execution with `Promise.all()`
  - Per-item error tracking
  - Consistent response formatting

- [ ] Unit tests for internal operations
  - Single item scenarios
  - Error handling and recovery
  - Validation logic

### Phase 2: Array-Based Work Items Tools (4-6 hours)

Prototype the unified pattern with work items domain:

- [ ] Implement `create_work_items` tool
  - Uses `createSingleWorkItem` from internal module
  - Accepts array of item specs
  - Returns consistent response format

- [ ] Implement `update_work_items` tool
  - Uses `updateSingleWorkItem` from internal module
  - Enhanced setters: assignedUserIds, tagIds, parentStoryId, designElementId
  - Per-item error tracking with indices

- [ ] Implement `delete_work_items` tool
  - Uses `deleteSingleWorkItem` from internal module
  - 2-step confirmation flow
  - Recovery cache integration

- [ ] Integration tests
  - Single-item calls (verify backward compat feel)
  - Batch calls (5-10 items)
  - Partial success scenarios
  - Error handling edge cases

### Phase 3: Auto-Create Tags Enhancement (2-3 hours)

Enable `update_work_items` to create tags on-the-fly:

- [ ] Tag resolution logic
  - Detect string tag names vs numeric IDs
  - Query existing tags from API
  - Auto-create missing tags
  - Replace string with ID before API call

- [ ] Category auto-create (optional enhancement)
  - Similar logic for categories
  - Helps during initial setup

- [ ] Warning/logging
  - Log when tags are auto-created
  - Helps users understand what happened

### Phase 4: Slim Mode Environment Flag (2-3 hours)

Make array-based tools optional:

- [ ] Add `HACKNPLAN_SLIM_MODE` environment variable
- [ ] Modify `createGlobalRegistry()` to conditionally register tools
- [ ] Tool categorization:
  - `core`: Always expose (30 tools)
  - `legacy`: Only in full mode (87 tools)
- [ ] Update documentation

### Phase 5: Apply Pattern to Other Domains (3-4 hours)

Extend array-based pattern:

- [ ] Comments: `add_comments`, `update_comments` (array-based)
- [ ] Boards: `create_boards`, `update_boards` (array-based)
- [ ] Projects: `create_projects` (array-based)
- [ ] Logging: `log_work_sessions` (array-based)

### Phase 6: Testing & Documentation (4-5 hours)

Comprehensive validation:

- [ ] Integration tests for all array-based tools
- [ ] Slim mode vs full mode verification
- [ ] Update API-REFERENCE.md with new tools
- [ ] Create MIGRATION-GUIDE.md
- [ ] Update README with slim mode documentation

---

## Benefits & Trade-Offs

### Benefits

| Benefit | Quantification |
|---------|----------------|
| Tool reduction | 117 → 30 (74% reduction) |
| Context savings | ~5,850 → 1,800 tokens (69% reduction) |
| Simpler mental model | No singular vs batch choice |
| Easier to use | Array pattern is transparent |
| Better scaling | Add items without new tools |
| Consistent responses | All tools return same format |
| Auto-create tags | Reduces friction during setup |
| Time tracking | Automatic elapsed time calculation |

### Trade-Offs

| Trade-Off | Mitigation |
|-----------|-----------|
| Breaking changes | Keep old tools in "full mode", deprecate gradually |
| Learning curve | Clear migration guide with examples |
| Partial success | Document that all items are processed independently |
| No transactions | Explain per-item semantics clearly |

---

## Design Decisions

### 1. Parallel vs Sequential Processing

**Decision:** Process array items in parallel using `Promise.all()`

**Rationale:**
- Better performance (especially for I/O-bound operations)
- No functional difference for independent items
- HacknPlan API supports concurrent requests
- Can handle 100+ items efficiently

**Code:**
```typescript
const results = await Promise.all(
  args.items.map(item => createSingleWorkItem(item, ctx))
);
```

### 2. Partial Success Semantics

**Decision:** Always return success with `{ successful: 3, failed: 2, errors: [...] }`

**Rationale:**
- Matches real-world behavior (some items succeed, some fail)
- Allows caller to decide whether to retry, skip, or abort
- Better than all-or-nothing semantics
- Consistent with batch operation expectations

### 3. Error Reporting

**Decision:** Include per-item errors with array indices

**Rationale:**
- Caller can identify exactly which items failed
- Enables retry logic (just resubmit failed items)
- Clear mapping between input and output errors

**Response:**
```typescript
{
  items: [...],
  errors: [
    { index: 2, error: "Title is required" },
    { index: 5, error: "Category not found" }
  ]
}
```

### 4. No Transaction Semantics

**Decision:** Process items independently, no rollback

**Rationale:**
- HacknPlan API doesn't support transactions
- Partial success is acceptable in practice
- Simpler to implement and reason about
- Caller retries failed items if needed

### 5. Array Length Limits

**Decision:** Max 100 items per call

**Rationale:**
- Matches HacknPlan API pagination limit
- Prevents memory issues
- Reasonable batch size for real workflows
- Documented in schema validation

---

## Backward Compatibility

### Migration Strategy

1. **Phase 1: Introduce new tools** (v7.3.0)
   - Add array-based tools alongside existing ones
   - Both work in parallel
   - No deprecation warnings yet

2. **Phase 2: Deprecate old tools** (v8.0.0)
   - Old tools still work
   - Add deprecation warnings in responses
   - Provide migration guide in docs

3. **Phase 3: Remove old tools** (v9.0.0)
   - Old tools no longer available
   - Full slim mode operation

### Slim Mode Flag

Users can opt in to old behavior:

```bash
# Full mode (117 tools, legacy behavior)
HACKNPLAN_SLIM_MODE=false node dist/index.js

# Slim mode (30 tools, new array-based API)
HACKNPLAN_SLIM_MODE=true node dist/index.js
```

Default: `HACKNPLAN_SLIM_MODE=false` (full mode for backward compat)

---

## References & Related Documentation

- **Project Repo:** `/cpp/hacknplan-mcp/`
- **Tool Analysis:** [[../../hacknplan-mcp/TOOL-ANALYSIS.md]]
- **Implementation Summary:** [[../../hacknplan-mcp/IMPLEMENTATION-SUMMARY.md]]
- **HacknPlan Task:** [#142 - Array-Based API Refactor](https://app.hacknplan.com/p/230809/workitems/142)

---

## Timeline & Resources

**Total Estimated Implementation:** 17-24 hours (~3-4 work days)

- Phase 1 (Extract): 2-3 hours
- Phase 2 (Prototype): 4-6 hours
- Phase 3 (Enhancement): 2-3 hours
- Phase 4 (Configuration): 2-3 hours
- Phase 5 (Extension): 3-4 hours
- Phase 6 (Testing): 4-5 hours

**Key Resources Needed:**
- TypeScript/Node.js development environment
- HacknPlan API access for testing
- Unit/integration test framework
- Documentation tools

---

## Success Criteria

- [x] Tool analysis complete (see TOOL-ANALYSIS.md)
- [ ] Phase 1: Internal operations extracted and tested
- [ ] Phase 2: create_work_items, update_work_items, delete_work_items working
- [ ] Phase 3: Auto-create tags functional
- [ ] Phase 4: HACKNPLAN_SLIM_MODE flag working
- [ ] Phase 5: All domains using array-based pattern
- [ ] Phase 6: Tests passing, docs updated, migration guide published

---

**Document Status:** Planning Phase  
**Last Updated:** 2025-12-21  
**Next Review:** After Phase 1 completion
