# 🚀 Next Agent: Start Here

**Date**: 2025-12-16
**Previous Session**: HacknPlan MCP Phase 3 Wave 1 (Task #8 Complete)
**Status**: ✅ Task #8 COMPLETE - Task #9 Ready

---

## What Just Happened

✅ **Completed Task #8: `get_my_current_tasks()` Smart Query Function**

**Implementation:**
- New file: `src/tools/queries.ts` - Smart query aggregation functions
- Modified: `src/tools/registry.ts` - Added queryTools to registry (93 tools total)
- Modified: `src/tool-integration.test.ts` - Added 5 comprehensive tests

**Features Delivered:**
```typescript
get_my_current_tasks({
  projectId?: number,         // Optional with HACKNPLAN_DEFAULT_PROJECT
  boardId?: number,           // Filter by sprint
  includeCompleted?: boolean  // Default: false
})

Returns:
{
  items: SlimWorkItem[],      // Token-efficient format
  total: number,
  byStage: { [stage]: count },
  byPriority: { [priority]: count },
  totalEstimate: number,
  inProgress: SlimWorkItem[]
}
```

**Build Status:**
- ✅ TypeScript compilation: PASS
- ✅ Unit tests: 19/19 PASS
- ✅ Tool count: 93 MCP tools

**Commit:** `35fc3cf` - feat(mcp): Implement get_my_current_tasks smart query function (Task #8)

**HacknPlan Status:**
- Task #8: COMPLETED (5 hours logged)
- Sprint Progress: 25.5 / 58 hours (44.0%)
- Design Element: #3 (Smart Query Functions)

---

## What You Need to Do Next

### Your Main Task: Task #9 - `get_blockers()` (5 hours)

**Goal**: Implement dependency graph analysis to identify blocking work items

**IMPORTANT:** Dependencies are a **premium HacknPlan feature**. The implementation MUST support:
1. **Premium Mode**: Use real dependency API endpoints
2. **Fallback Mode**: Parse dependency markers from task descriptions (e.g., "Blocked by: #123, #456")

**Expected Function Signature:**
```typescript
get_blockers({
  projectId?: number,
  workItemId?: number | string,  // Optional - analyze specific item or all
  includeIndirect?: boolean,      // Default: false - transitive dependencies
  useFallbackParser?: boolean     // Default: true - parse description markers if API fails
})

Returns:
{
  blocked: WorkItem[],            // Items blocked by incomplete dependencies
  blockers: WorkItem[],           // Items blocking others
  criticalPath?: {                // If workItemId specified
    item: WorkItem,
    prerequisites: WorkItem[],
    depth: number
  },
  source: 'api' | 'description-markers'  // How dependencies were detected
}
```

### Implementation Steps

1. **Read existing code for patterns:**
   ```bash
   cd /c/cpp/hacknplan-mcp
   cat src/tools/queries.ts      # See get_my_current_tasks pattern
   cat src/tools/advanced.ts     # See get_dependency_tree function
   ```

2. **Add `get_blockers()` to `src/tools/queries.ts`:**

   **Two-Mode Implementation:**

   **A. Premium Mode (Dependency API):**
   - Try `GET /projects/{projectId}/workitems/{workItemId}/dependencies` first
   - If successful, build graph from API response
   - Mark source as 'api'

   **B. Fallback Mode (Description Parser):**
   - If API returns 403/404/premium error, fall back to description parsing
   - Parse patterns from work item descriptions:
     - `Blocked by: #123` or `Blocked by #123`
     - `Depends on: #456, #789` or `Depends on #456, #789`
     - `Prerequisites: #101, #102`
     - `Requires: #123`
   - Build dependency graph from parsed markers
   - Mark source as 'description-markers'

   **Graph Processing (Both Modes):**
   - Build adjacency graph (Map<workItemId, predecessorIds[]>)
   - Find all incomplete prerequisites
   - Implement critical path calculation (BFS/DFS)
   - Handle circular dependencies gracefully
   - Return slim work items for token efficiency

3. **Update `src/tools/queries.ts` export:**
   ```typescript
   export const queryTools: ToolDefinition[] = [
     getMyCurrentTasks,
     getBlockers,  // ← ADD THIS
   ];
   ```

4. **Add test coverage in `src/tool-integration.test.ts`:**
   - **API Mode Tests:**
     - Basic blocker detection (simple chain: A → B → C)
     - Indirect dependency analysis (`includeIndirect: true`)
     - Critical path calculation (specific `workItemId`)
     - Empty dependency graph (no blockers)
   - **Fallback Mode Tests:**
     - Description parsing with various marker patterns
     - Mixed markers (multiple patterns in one description)
     - Invalid/malformed markers (graceful handling)
     - No markers found (empty result)
   - **Tool Registry:**
     - Verify get_blockers registered

5. **Build and test:**
   ```bash
   npm run build && npm test
   ```

6. **Commit:**
   ```bash
   git add src/tools/queries.ts src/tool-integration.test.ts
   git commit -m "[Use commit template below]"
   ```

7. **Update HacknPlan:**
   - Log 5 hours on Task #9
   - Move to "Done" stage

---

## Key Files to Know

| File | What It Does |
|------|--------------|
| `src/tools/queries.ts` | Smart query functions (get_my_current_tasks, get_blockers) |
| `src/tools/advanced.ts` | Advanced operations (get_dependency_tree reference) |
| `src/tools/registry.ts` | Central tool registration (93 tools) |
| `src/tool-integration.test.ts` | Integration test suite |
| `src/index.ts` | MCP server entry point |

**Architecture Pattern:**
```
Smart Queries → httpRequest → HacknPlan API
      ↓
  Metadata Cache (stages, priorities)
      ↓
  Aggregation Logic (byStage, byPriority)
      ↓
  Slim Work Items (token efficiency)
```

---

## Quick Commands

```bash
# Build HacknPlan MCP
cd /c/cpp/hacknplan-mcp
npm run build

# Run tests
npm test

# Check git status
git status
git log --oneline -5

# See latest commit
git show 35fc3cf --stat
```

---

## Environment Setup

**Required:**
```bash
set HACKNPLAN_API_KEY=your_key_here
set HACKNPLAN_DEFAULT_PROJECT=230954
```

**Files:**
- Repository: `/c/cpp/hacknplan-mcp/`
- Latest commit: `35fc3cf` (Task #8)
- Next task: Task #9 (get_blockers)

---

## Architecture Context

**TypeScript Project Structure:**
```
src/tools/
├── queries.ts         # ← PHASE 3: Smart queries (YOU ARE HERE)
├── workflow.ts        # ← PHASE 2: Workflow shortcuts (DONE)
├── utilities.ts       # ← PHASE 1: Name resolution (DONE)
├── advanced.ts        # Bulk operations, dependency tree
├── registry.ts        # Central tool registration
└── types.ts          # Shared types
```

**Key Concepts:**
1. **Smart Queries**: Aggregate data server-side, return statistics + slim items
2. **Token Efficiency**: Slim work items save 73% tokens per item
3. **Metadata Caching**: Stages, categories, priorities cached per project
4. **Name Resolution**: Task names → IDs, project names → IDs

---

## Remaining Work (~32.5 hours)

**Phase 3 Wave 1 (HIGH PRIORITY):**
- [ ] **Task #9**: `get_blockers()` - Dependency graph analysis (5h) **← START HERE**

**Phase 3 Wave 2:**
- [ ] **Task #11**: `get_sprint_progress()` - Enhanced sprint overview (8h)

**Phase 4: Bulk Operations + Comments:**
- [ ] **Task #15**: Verify `batch_assign_tags()` (3h)
- [ ] **Task #17**: `move_tasks_to_stage()` (4h)
- [ ] **Task #14**: Markdown comment support (4h)
- [ ] **Task #16**: `batch_complete_tasks()` (6h)

**Estimated Wall Time:** ~16 hours (with parallel execution)

---

## Dependency Fallback Parser (Premium Feature Workaround)

**Why This Matters:**
Dependencies are a premium HacknPlan feature. Users without premium can use structured markers in task descriptions as a workaround.

**Supported Marker Patterns:**
```typescript
// Regex patterns to match (case-insensitive):
const patterns = [
  /Blocked by:?\s*#(\d+(?:\s*,\s*#\d+)*)/gi,
  /Depends on:?\s*#(\d+(?:\s*,\s*#\d+)*)/gi,
  /Prerequisites?:?\s*#(\d+(?:\s*,\s*#\d+)*)/gi,
  /Requires?:?\s*#(\d+(?:\s*,\s*#\d+)*)/gi,
];
```

**Example Task Descriptions:**
```markdown
# Task #123: Implement shader system
Blocked by: #101, #102
Requires proper device initialization.

# Task #456: Add ray marching
Depends on #123
Prerequisites: #101
This needs the shader system first.
```

**Parser Algorithm:**
1. Fetch all work items for project
2. For each item, extract description
3. Apply regex patterns to find dependency markers
4. Parse comma-separated task IDs
5. Validate IDs exist in project
6. Build dependency graph from markers

**Error Handling:**
- Invalid task IDs → skip with warning
- Malformed markers → skip gracefully
- No markers found → return empty dependency set
- API 403/404 → automatically fall back to parser

**Configuration:**
- `useFallbackParser: true` (default) - Auto-fallback on API failure
- `useFallbackParser: false` - Fail if API unavailable (premium-only mode)

---

## Watch Out For

1. **TypeScript Type Safety**
   - Use `as any` for mock contexts in tests
   - Cast httpRequest returns: `await ctx.httpRequest('/endpoint') as any`
   - Explicit type assertions for Map operations: `stageMap.get(id) as string`

2. **Dependency Graph Complexity**
   - Handle circular dependencies gracefully
   - Use Set to track visited nodes (prevent infinite loops)
   - Consider max depth limit for critical path
   - **NEW:** Support both API and description-parser sources

3. **Premium Feature Detection**
   - Catch 403 Forbidden (no premium access)
   - Catch 404 Not Found (endpoint doesn't exist on free tier)
   - Gracefully fall back to description parser
   - Include `source` field in response to indicate method used

4. **Token Efficiency**
   - Always return slim work items for collections
   - Cache user ID (fetch once, reuse)
   - Aggregate statistics server-side
   - **NEW:** Description parser must fetch all items - cache results

5. **Testing Patterns**
   - Wrap async mock functions: `(async () => { ... }) as any`
   - Always provide complete getMetadata mock
   - Test empty results (no dependencies, no blockers)
   - **NEW:** Test both API mode and fallback mode
   - **NEW:** Mock API 403 errors to trigger fallback

6. **Commit Hygiene**
   - One feature per commit (atomic changes)
   - Include build status in commit message
   - Reference design element #3

---

## HacknPlan Context

**Project**: 230954 (HacknPlan MCP)
**Sprint**: 650172 - API Enhancement Sprint 1

**Completed Tasks (7 of 13):**
- #10: `resolveWorkItemId()` (3h) - Commit: `b664497`
- #18: `resolveProjectIdFromName()` (3h) - Commit: `b664497`
- #13: Design element validation fix (3h) - Commit: `b664497`
- #6: `create_subtask()` (4h) - Commit: `df19b95`
- #12: `start_task()` (2h) - Commit: `df19b95`
- #7: `complete_task()` (3h) - Commit: `8605d02`
- #8: `get_my_current_tasks()` (5h) - Commit: `35fc3cf` ✅ JUST DONE

**Your Task**: #9 - `get_blockers()` (5h) **← START HERE**

**Sprint Progress**: 25.5 / 58 hours (44.0%)

---

## Documentation

**Session Summary:**
[Vixen-Docs/Sessions/2025-12-16-glue-mcp-sync-engine.md](C:/cpp/VBVS--VIXEN/VIXEN/Vixen-Docs/Sessions/2025-12-16-glue-mcp-sync-engine.md)

**Phase 3 Spec:**
[Vixen-Docs/Sessions/phase3-smart-queries-spec.md](C:/cpp/VBVS--VIXEN/VIXEN/Vixen-Docs/Sessions/phase3-smart-queries-spec.md)

**API Documentation:**
[hacknplan-obsidian-glue/docs/API.md](C:/cpp/hacknplan-obsidian-glue/docs/API.md)

---

## If You Get Stuck

**Reference Implementation:**
- Look at `src/tools/queries.ts` for `get_my_current_tasks()` pattern
- Look at `src/tools/advanced.ts` for `get_dependency_tree()` reference
- Look at `src/tool-integration.test.ts` for test patterns

**Helpful Commands:**
```bash
# See what changed in last commit
git show 35fc3cf --stat

# Find dependency-related code
grep -r "dependencies" src/tools/

# Check TypeScript errors
npm run build 2>&1 | grep "error TS"
```

**HacknPlan API Endpoints:**
```
GET /projects/{projectId}/workitems/{workItemId}/dependencies
```

**Returns:**
```json
{
  "predecessors": [{ "workItemId": 123, "title": "...", "isCompleted": false }],
  "successors": [{ "workItemId": 456, "title": "...", "isCompleted": false }]
}
```

---

## Success Criteria for Task #9

✅ `get_blockers()` function implemented in `src/tools/queries.ts`
✅ Dependency graph traversal algorithm works
✅ Handles circular dependencies gracefully
✅ Implements critical path calculation (when `workItemId` specified)
✅ Returns slim work items for token efficiency
✅ Supports `includeIndirect` for transitive dependencies
✅ Registered in tool registry (should be automatic)
✅ 4-5 comprehensive tests added
✅ TypeScript compiles without errors
✅ All tests pass (npm test)
✅ Clean commit with proper message
✅ HacknPlan task #9 completed (5h logged)

---

## Commit Message Template

```
feat(mcp): Implement get_blockers dependency analysis with fallback (Task #9)

Add smart query function to analyze dependency graphs and identify
blocking work items with premium API + free-tier fallback support.

**Implementation:**
- New function: get_blockers() in src/tools/queries.ts
- **Dual-mode operation:**
  - Premium Mode: Uses HacknPlan dependency API
  - Fallback Mode: Parses description markers (Blocked by, Depends on, etc.)
- Dependency graph traversal with cycle detection
- Critical path calculation for specific work items
- Support for transitive dependency analysis (includeIndirect)
- Returns slim work items for token efficiency

**Fallback Parser:**
- Supports multiple marker patterns:
  - "Blocked by: #123, #456"
  - "Depends on: #789"
  - "Prerequisites: #101"
  - "Requires: #102"
- Graceful error handling for malformed markers
- Automatic fallback on 403/404 API errors
- Includes 'source' field in response ('api' or 'description-markers')

**Algorithm:**
- Try premium API first
- On failure (403/404), fall back to description parsing
- Build adjacency graph (Map<workItemId, predecessorIds[]>)
- Traverse graph using BFS/DFS
- Track visited nodes to prevent infinite loops
- Filter incomplete prerequisites
- Calculate critical path depth

**Test Coverage:**
- API Mode: Basic blocker detection, indirect deps, critical path
- Fallback Mode: Pattern parsing, mixed markers, malformed markers
- Circular dependency handling
- Empty dependency graph (no blockers)
- Premium feature detection (403/404 handling)

**Build Status:**
- TypeScript compilation: ✓ PASS
- Unit tests (npm test): ✓ N/N PASS

Design Element: #3 - Smart Query Functions
Sprint: 650172 - API Enhancement Sprint 1
Project: HacknPlan MCP (ID: 230954)

🤖 Generated with [Claude Code](https://claude.com/claude-code)

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>
```

---

**Good luck! Task #8 is complete. The pattern is established. Build on it for #9.** 🚀

*Last updated: 2025-12-16 by Claude Sonnet 4.5 (Task #8 handoff)*
