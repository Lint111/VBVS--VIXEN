# Phase 3: Smart Query Functions - Implementation Specifications

**Sprint:** 650172 - API Enhancement Sprint 1
**Design Element:** #3 - Smart Query Functions
**Estimated Total:** 18 hours (sequential) → ~10 hours (parallel)
**Tasks:** #8, #9, #11

---

## Task #8: get_my_current_tasks()

**Estimated:** 5 hours
**Dependencies:** None (parallel with #9)

### Function Signature
```typescript
interface GetMyCurrentTasksArgs {
  projectId?: number;  // Optional if HACKNPLAN_DEFAULT_PROJECT set
  boardId?: number;    // Filter by specific sprint
  includeCompleted?: boolean;  // Default: false
}

interface GetMyCurrentTasksResult {
  items: HacknPlanWorkItem[];
  total: number;
  byStage: Map<string, number>;  // Task count per stage
  byPriority: Map<string, number>;  // Task count per importance level
  totalEstimate: number;  // Sum of estimated costs
  inProgress: HacknPlanWorkItem[];  // Quick access to active tasks
}
```

### Behavior
1. Get current user ID via `get_current_user`
2. Filter work items:
   - Assigned to current user
   - Not completed (unless `includeCompleted: true`)
   - Optionally filtered by `boardId`
3. Aggregate statistics:
   - Group by stage (count per stage)
   - Group by priority (count per importance level)
   - Calculate total estimated hours
   - Extract in-progress tasks for quick access
4. Return slim work items to minimize token usage

### Implementation Location
- File: `src/tools/queries.ts` (new file)
- Register in `src/tools/registry.ts`
- Export from `src/index.ts`

### Testing
- Mock current user ID
- Test filtering by project/board
- Verify aggregation statistics
- Test includeCompleted flag

---

## Task #9: get_blockers()

**Estimated:** 5 hours
**Dependencies:** None (parallel with #8)

### Function Signature
```typescript
interface GetBlockersArgs {
  projectId?: number;
  workItemId?: number | string;  // If specified, only get blockers for this item
  includeIndirect?: boolean;  // Include transitive dependencies (default: false)
}

interface GetBlockersResult {
  blockedItems: Array<{
    workItem: HacknPlanWorkItem;
    blockedBy: HacknPlanWorkItem[];  // Direct blockers
    blockerCount: number;
    allBlockerIds: number[];  // Includes indirect if includeIndirect=true
  }>;
  totalBlockedItems: number;
  criticalPath?: {  // If workItemId specified
    depth: number;
    chain: HacknPlanWorkItem[];  // Ordered from root blocker to target
  };
}
```

### Behavior
1. Get all work items with dependencies
2. Build dependency graph
3. For each incomplete item:
   - Identify direct blockers (predecessors that aren't completed)
   - Optionally calculate transitive blockers
   - Count total blockers
4. If `workItemId` specified:
   - Calculate critical path (longest chain of blockers)
   - Return ordered chain from root to target
5. Return items sorted by blocker count (most blocked first)

### Implementation Location
- File: `src/tools/queries.ts`
- Register in `src/tools/registry.ts`
- Export from `src/index.ts`

### Algorithm
```typescript
// Pseudo-code for blocker detection
function getBlockers(workItemId, dependencies, visited = new Set()) {
  if (visited.has(workItemId)) return [];
  visited.add(workItemId);

  const directBlockers = dependencies
    .filter(d => d.successorId === workItemId && !d.predecessor.isCompleted)
    .map(d => d.predecessor);

  if (includeIndirect) {
    const indirectBlockers = directBlockers.flatMap(b =>
      getBlockers(b.workItemId, dependencies, visited)
    );
    return [...directBlockers, ...indirectBlockers];
  }

  return directBlockers;
}
```

### Testing
- Mock dependency graph with 3-level chain
- Test direct blocker detection
- Test transitive blocker detection
- Test critical path calculation
- Test sorting by blocker count

---

## Task #11: get_sprint_progress() - Enhanced

**Estimated:** 8 hours
**Dependencies:** May use #8/#9 results (launch after #8/#9 complete)

### Function Signature
```typescript
interface GetSprintProgressArgs {
  projectId?: number;
  boardId: number;  // Required - which sprint
}

interface GetSprintProgressResult {
  // Existing fields (from current implementation)
  boardId: number;
  boardName: string;
  totalTasks: number;
  completedTasks: number;
  inProgressTasks: number;
  todoTasks: number;
  completionPercentage: number;
  byStage: Record<string, number>;

  // NEW: Enhanced fields
  burndown: {
    idealRemaining: number[];     // Ideal burndown line
    actualRemaining: number[];    // Actual remaining work
    dates: string[];              // Date labels for each point
    currentVelocity: number;      // Tasks/day completion rate
    projectedCompletion: string;  // Estimated completion date
  };

  estimateTracking: {
    totalEstimated: number;       // Sum of all estimates
    totalCompleted: number;       // Sum of completed estimates
    totalRemaining: number;       // Sum of incomplete estimates
    estimatedHoursPerDay: number; // Average completion rate
    onTrack: boolean;             // Will finish by sprint end?
    daysOverUnder: number;        // Days ahead/behind schedule
  };

  teamUtilization: Array<{
    userId: number;
    userName: string;
    assignedTasks: number;
    completedTasks: number;
    inProgressTasks: number;
    totalEstimate: number;
    utilizationPercentage: number;  // % of team's total work
  }>;

  blockers: {
    blockedCount: number;
    criticalBlockers: Array<{  // Top 5 most blocking items
      workItemId: number;
      title: string;
      blockingCount: number;  // How many items it blocks
    }>;
  };
}
```

### Behavior
1. Get existing sprint summary (reuse current implementation)
2. Get sprint date range from board metadata
3. Calculate burndown data:
   - Query work sessions to track completion over time
   - Calculate ideal burndown (linear from start to end)
   - Calculate actual burndown (actual completion dates)
   - Project completion based on current velocity
4. Calculate estimate tracking:
   - Sum estimated costs for all/completed/remaining items
   - Calculate average hours completed per day
   - Compare to sprint end date
5. Calculate team utilization:
   - Group tasks by assigned user
   - Calculate per-user workload
   - Show % of team's total work
6. Identify blockers:
   - Count blocked items
   - Find items blocking the most other items
7. Return comprehensive sprint health report

### Implementation Location
- File: `src/tools/queries.ts`
- Enhance existing `get_sprint_summary` or create new function
- Register in `src/tools/registry.ts`
- Export from `src/index.ts`

### Testing
- Mock sprint with date range
- Mock work items with dates/estimates
- Test burndown calculation
- Test velocity projection
- Test team utilization grouping
- Test blocker detection integration

---

## Parallel Execution Strategy

### Wave 1: Launch Simultaneously (5h wall time)
- Agent A: Task #8 `get_my_current_tasks()`
- Agent B: Task #9 `get_blockers()`

Both agents are independent, no shared state.

### Wave 2: Launch After Wave 1 (8h wall time)
- Agent C: Task #11 `get_sprint_progress()` (enhanced)

Wait for #8/#9 to complete first. May reuse patterns from #8 (filtering) and #9 (blocker detection).

**Total Wall Time:** ~13 hours (vs 18 hours sequential)
**Time Saved:** 5 hours (28%)

---

## Implementation Notes

### Shared Utilities

All query functions should use:
- `ctx.slimWorkItem()` to minimize token usage
- Metadata caching for stage/user/priority lookups
- Clear error messages for missing data
- Consistent result structure with summary stats

### File Structure

```
src/tools/queries.ts
├── getMyCurrentTasks
├── getBlockers
└── getSprintProgress (enhanced)
```

### Token Optimization

- Use slim work items by default
- Return only necessary fields
- Aggregate statistics instead of full lists where possible
- Cache metadata lookups

---

## Success Criteria

- [ ] All 3 functions implemented
- [ ] Proper name resolution support
- [ ] Comprehensive test coverage
- [ ] Build passes (`npm run build`)
- [ ] All tests pass (`npm test`)
- [ ] Clean commits referencing tasks #8, #9, #11
- [ ] Functions registered in tool registry
- [ ] Documentation in function descriptions

---

**Next Actions:**
1. Wait for Phase 2 agents to complete
2. Launch Phase 3 Wave 1 (agents for #8, #9)
3. Wait for Wave 1 completion
4. Launch Phase 3 Wave 2 (agent for #11)
5. Consolidate results and update sprint progress
