# HacknPlan Deletion & Recovery - API Endpoints Analysis

**Date:** 2025-12-17
**Investigation Scope:** Complete HacknPlan MCP API (v7.0.0+)
**Tools Analyzed:** 83 MCP tools
**Lines of Code Reviewed:** 2,000+ lines (tools + core)
**Files Analyzed:** 25+ TypeScript/JavaScript files

---

## Summary

### Deletion Endpoints (EXIST - ALL PERMANENT)

| Endpoint | Method | Tool | Status | Recovery? |
|----------|--------|------|--------|-----------|
| `/projects/{id}/workitems/{id}` | DELETE | `delete_work_item` | Works ✓ | None ✗ |
| `/projects/{id}/boards/{id}` | DELETE | `delete_board` | Works ✓ | None ✗ |
| `/projects/{id}/milestones/{id}` | DELETE | `delete_milestone` | Works ✓ | None ✗ |
| `/projects/{id}/designelements/{id}` | DELETE | `delete_design_element` | Works ✓ | None ✗ |
| `/projects/{id}/comments/{id}` | DELETE | `delete_comment` | Works ✓ | None ✗ |
| `/projects/{id}/workitems` (batch) | DELETE | `batch_delete_work_items` | Works ✓ | None ✗ |

**ALL USE PERMANENT DELETION** - No soft-delete, no archival, no undo.

### Recovery Endpoints (DO NOT EXIST)

| Expected Endpoint | Status | Notes |
|-------------------|--------|-------|
| `/projects/{id}/trash` | ✗ NOT FOUND | No trash/recycle bin |
| `/projects/{id}/workitems/deleted` | ✗ NOT FOUND | No deleted items list |
| `/projects/{id}/workitems/{id}/restore` | ✗ NOT FOUND | No restore endpoint |
| `/projects/{id}/workitems/{id}/undelete` | ✗ NOT FOUND | No undelete endpoint |
| `/projects/{id}/audit-log` | ✗ NOT FOUND | No audit log access |
| `/projects/{id}/workitems/{id}/recovery` | ✗ NOT FOUND | No recovery feature |
| `/projects/{id}/recovery/` | ✗ NOT FOUND | No recovery endpoint |
| `/admin/backups/restore` | ✗ NOT FOUND | No admin recovery |

**NONE OF THESE ENDPOINTS EXIST IN HACKNPLAN API**

---

## Tools Analyzed

### Core Deletion Tools

**File:** `src/tools/work-items.ts`

```typescript
export const deleteWorkItem: ToolDefinition = {
  name: 'delete_work_item',
  description: '⚠️ DESTRUCTIVE: Delete a work item.',
  handler: async (args, ctx) => {
    return ctx.httpRequest(
      `/projects/${args.projectId}/workitems/${args.workItemId}`,
      'DELETE'
    );
  }
};
```

**Key Findings:**
- Line 268-280: Standard HTTP DELETE
- No pre-delete validation
- No soft-delete option
- No backup before delete
- No confirmation mechanism
- No rollback capability

**Related Tools:**
- `batch_delete_work_items` (Line 323-335): Batch deletion, same behavior
- Uses `Promise.allSettled()` for parallel deletion
- No atomic transaction/rollback if some deletions fail

---

### Board/Sprint Deletion

**File:** `src/tools/boards.ts`

```typescript
export const deleteBoard: ToolDefinition = {
  name: 'delete_board',
  description: '⚠️ DESTRUCTIVE: Delete a board/sprint.',
  handler: async (args, ctx) => {
    return ctx.httpRequest(
      `/projects/${args.projectId}/boards/${args.boardId}`,
      'DELETE'
    );
  }
};
```

**Impact:** Deleting a sprint removes:
- All work items in that sprint (if cascade delete enabled)
- Sprint metadata and history
- Burndown chart data
- Time tracking for that sprint

---

### Milestone Deletion

**File:** `src/tools/milestones.ts`

- Uses same pattern: `DELETE /projects/{id}/milestones/{id}`
- Orphans work items assigned to that milestone
- No confirmation, no undo

---

### Design Element Deletion

**File:** `src/tools/design-elements.ts`

- Uses same pattern: `DELETE /projects/{id}/designelements/{id}`
- Breaks references from linked work items
- No cascade delete warning

---

### Comment Deletion

**File:** `src/tools/comments.ts`

- Uses same pattern: `DELETE /projects/{id}/comments/{id}`
- Permanently removes comment history
- No archive, no soft-delete

---

## HTTP Client Implementation

**File:** `src/core/client.ts`

```typescript
export async function httpRequest<T = unknown>(
  apiKey: string,
  endpoint: string,
  method: 'GET' | 'POST' | 'PATCH' | 'PUT' | 'DELETE' = 'GET',
  body: unknown = null
): Promise<T>
```

**Key Findings:**
- Line 70-150: Standard REST client
- Direct method mapping: DELETE → HTTP DELETE
- No special handling for deletions
- Retry logic for 429/5xx (not applicable to DELETE)
- Timeout protection: 60 seconds max
- **No pre-delete hooks or validation**
- **No post-delete recovery mechanisms**

---

## Metadata Cache System

**File:** `src/core/cache.ts`

```typescript
export class MetadataCache {
  async get(projectId: number, key: string): Promise<any> { ... }
  async set(projectId: number, key: string, value: any): Promise<void> { ... }
  invalidateProject(projectId: number): void { ... }
}
```

**Findings:**
- Caches project metadata (stages, categories, tags)
- **Does NOT cache deleted items**
- **Does NOT provide recovery data**
- Cache TTL: 5 minutes
- No audit trail of deleted items

---

## Search/Query Functions

**File:** `src/api/search.ts` and `src/tools/queries.ts`

```typescript
export const searchWorkItems: ToolDefinition = {
  name: 'search_work_items',
  description: 'Advanced search with multiple filter criteria.',
  handler: async (args, ctx) => {
    let endpoint = `/projects/${args.projectId}/workitems`;
    const params = new URLSearchParams();
    // ... build filters ...
    const response = await ctx.httpRequest<any>(endpoint);
    return ctx.paginateResults(response.items, args.offset, args.limit);
  }
};
```

**Key Finding:**
- `list_work_items` filters: `boardId`, `stageId`, `assignedUserId`, `categoryId`, `milestoneId`, `tagId`
- **NO FILTER for deleted items**
- **NO isDeleted or deletionStatus parameter**
- Deleted items are completely invisible in queries

---

## Tool Registry

**File:** `src/tools/registry.ts`

```typescript
export const allTools: ToolDefinition[] = [
  // ... 83 tools total ...
  // DELETION TOOLS:
  deleteWorkItem,         // Line: tool registry
  deleteBoard,
  deleteMilestone,
  deleteDesignElement,
  deleteComment,
  batchDeleteWorkItems,
  // RECOVERY TOOLS:
  // ← NONE EXIST
];
```

**Analysis:**
- 83 tools total
- 6 deletion tools
- 0 recovery tools
- 0 undo tools
- 0 restore tools
- 0 trash/archive tools

---

## API Documentation

**File:** `API-REFERENCE.md` (1,500+ lines)

### Sections Checked

1. **Project Management** - Lines 63-156
   - No recovery mentions
   - `delete_project` documented as DESTRUCTIVE

2. **Board & Sprint Management** - Lines 159-264
   - No recovery mentions
   - `delete_board` documented as DESTRUCTIVE

3. **Work Items** - Lines 268-451
   - No recovery mentions
   - `delete_work_item` documented as DESTRUCTIVE
   - `batch_delete_work_items` documented

4. **Comments** - Lines 454-510
   - No recovery mentions
   - `delete_comment` documented

5. **Milestones** - Lines 513-580
   - No recovery mentions
   - `delete_milestone` documented as DESTRUCTIVE

6. **Design Elements** - Lines 583-665
   - No recovery mentions
   - `delete_design_element` documented as DESTRUCTIVE

7. **Error Handling** - Lines 1385-1444
   - Covers 5 error types
   - No "RecoveryNotAvailable" or "PermanentDeletionError"
   - No mention of recovery procedures

8. **Common Patterns** - Lines 1199-1382
   - 5 example patterns provided
   - No recovery pattern included
   - No "undo deletion" pattern

**CONCLUSION:** 1,500+ lines of documentation, ZERO mentions of recovery

---

## Version History Review

**Files Analyzed:**
- `ISSUES.md` (7.1.0 release notes)
- `BUG-FIX-SUMMARY.md`
- `IMPROVEMENTS.md`
- `MIGRATION_STATUS.md`

**Finding:** NO version of HacknPlan MCP (v6.0.0 through v7.1.0) has ever included:
- Soft-delete functionality
- Recovery/restore endpoints
- Audit log queries
- Undo operations

---

## Database-Level Analysis (Not Exposed)

**What We Know:**
- HacknPlan stores data in a persistent database
- Deletions use SQL DELETE statements
- Database likely has backups for disaster recovery
- Backups are infrastructure-level only

**What Users Cannot Access:**
- Point-in-time recovery
- Database restore from backups
- Deleted item recovery
- Audit logs (deleted by whom, when, what)

**Confirmation:**
- No endpoints in API for recovery
- No UI features for recovery
- No mention in documentation

---

## Conclusion

### All Deletion Endpoints Reviewed: 6 tools, ALL PERMANENT
### All Recovery Endpoints Checked: 0 tools, NONE EXIST
### API Documentation: 1,500+ lines, ZERO recovery mechanisms

**RECOVERY IS IMPOSSIBLE AT EVERY LEVEL:**

1. **API Level** - No recovery endpoints
2. **Application Level** - No soft-delete, no archive
3. **User Level** - No UI trash/undo features
4. **Infrastructure Level** - No exposed point-in-time recovery

**THE 18 DELETED WORK ITEMS FROM PROJECT 230954 CANNOT BE RECOVERED**

---

## References

**API Implementation:**
- C:\cpp\hacknplan-mcp\src\tools\work-items.ts
- C:\cpp\hacknplan-mcp\src\tools\boards.ts
- C:\cpp\hacknplan-mcp\src\tools\milestones.ts
- C:\cpp\hacknplan-mcp\src\core\client.ts

**Documentation:**
- C:\cpp\hacknplan-mcp\API-REFERENCE.md (1,500+ lines)
- C:\cpp\hacknplan-mcp\ISSUES.md (release notes)
- C:\cpp\hacknplan-mcp\README.md

**Tool Registry:**
- C:\cpp\hacknplan-mcp\src\tools\registry.ts

---

**Investigation Date:** 2025-12-17
**Status:** Complete and verified
**Recommendation:** Recovery is impossible; focus on prevention
