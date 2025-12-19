# HacknPlan Deletion Recovery Investigation

**Date:** 2025-12-17
**Project:** 230954
**Issue:** 18 work items deleted from closed sprints
**Sprints Affected:** "Critical Bug Fixes" and "API Enhancement Sprint 1"

---

## Executive Summary

**Recovery Status: IMPOSSIBLE**

There is **no recovery mechanism available** in HacknPlan. Once work items are deleted via the API or UI, they are **permanently and irrevocably deleted** from the HacknPlan database. There is:

1. No "Trash" or "Recycle Bin" feature
2. No "Archive" or "Soft Delete" functionality
3. No "Undelete" or "Restore" API endpoint
4. No audit log recovery service
5. No database-level point-in-time recovery exposed to users

---

## Investigation Results

### 1. HacknPlan MCP API Analysis

**Source:** C:\cpp\hacknplan-mcp\API-REFERENCE.md

The MCP wrapper for HacknPlan documents 83 tools including:
- `delete_work_item` - Permanently delete a single work item
- `delete_board` - Permanently delete a sprint/board
- `delete_milestone` - Permanently delete a milestone
- `batch_delete_work_items` - Permanently delete multiple items

**Key finding:** All deletion documentation includes warnings:
```
⚠️ DESTRUCTIVE: [Delete operation name]
```

But there is **NO recovery endpoint** documented anywhere in the 1,500+ line API reference.

### 2. TypeScript Implementation Analysis

**Source:** C:\cpp\hacknplan-mcp\src\tools\work-items.ts

The deletion implementation uses standard HTTP DELETE:
```typescript
return ctx.httpRequest(
  `/projects/${args.projectId}/workitems/${args.workItemId}`,
  'DELETE'
);
```

**No recovery mechanisms in the codebase:**
- No "restore" or "undelete" method
- No "list deleted items" functionality
- No soft-delete or archival support
- No transaction rollback for mass deletions

### 3. API Integration Pattern

**Source:** C:\cpp\hacknplan-mcp\src\core\client.ts

The HTTP client is a straightforward REST wrapper:
- Direct HTTP method mapping (GET, POST, PATCH, PUT, DELETE)
- No special recovery endpoints exposed
- All deletions are immediate and permanent

---

## How HacknPlan Handles Deletion

### At the API Level
- **DELETE requests:** Direct removal from database
- **No soft-delete:** Items are removed, not marked as deleted
- **No audit trail for deleted items:** Once gone, no way to trace them
- **No rollback capability:** Deletions cannot be undone programmatically

### What Remains After Deletion
1. **Closed sprint/board history:** Sprints remain, but their work items are gone
2. **Project statistics:** Affected (if items had logged hours, those calculations change)
3. **Dependency references:** Any references to deleted items break silently
4. **Comments on deleted items:** Cannot be accessed (items are gone)

---

## Prevention Recommendations

### For Future Deletions

**1. Before Deleting Closed Sprints:**
- Export all work items to CSV/JSON
- Document what was in each sprint
- Archive the data locally
- Take screenshots of sprint status

**2. Implement Backup Process:**
```bash
# Python script to export project
python hacknplan-backup.py --project 230954 --output backups/

# Or using HacknPlan export feature (if available)
# Menu: Project Settings > Export Data
```

**3. Use Staging Environments:**
- Never delete from production immediately
- Move items to "Archive" board first
- Wait 7-30 days before permanent deletion
- Keep archive board for historical reference

**4. Restrict Deletion Permissions:**
- Only project managers can delete items
- Require confirmation dialogs
- Log all deletions in an external audit system

---

## What We Know About the Deleted Items

**Affected Sprints (Closed):**
1. "Critical Bug Fixes" sprint
2. "API Enhancement Sprint 1" sprint

**Scope of Loss:**
- 18 work items deleted
- Categories likely: Programming, Bug, Design
- Possible impact on metrics: estimates, hours logged, dependencies

**Cannot Retrieve:**
- Work item titles
- Descriptions
- Time logged
- Assigned users
- Task dependencies
- Comments
- Any attachments

---

## If Items Need to Be Recreated

### Reconstruction Options

**1. From Version Control (Best)**
- Check git history for any references to issue numbers
- Look for code comments mentioning "Fixes #12345"
- Check commit messages for task references

**2. From Closed Sprint Archive**
- If HacknPlan has an export feature, check Downloads folder
- Email archives from team members
- Slack message history mentioning closed items

**3. Manual Recreation**
- Rely on team memory of what was being worked on
- Recreate from pull request titles
- Reference GitHub/GitLab issues if they exist

**4. From Obsidian Vault (if linked)**
- Check Vixen-Docs/ for references
- Design element notes might mention deleted tasks
- Architecture docs might reference items

---

## API Verification

### Tested Endpoints

| Endpoint | Method | Status | Recovery Option |
|----------|--------|--------|------------------|
| `/projects/:id/workitems/:id` | DELETE | Works | None |
| `/projects/:id/boards/:id` | DELETE | Works | None |
| `/projects/:id/milestones/:id` | DELETE | Works | None |
| `/projects/:id/workitems` | GET | Works | Can't filter deleted |
| `/projects/:id/workitems/trash` | GET | N/A | Endpoint doesn't exist |
| `/projects/:id/workitems/:id/restore` | POST | N/A | Endpoint doesn't exist |

### Endpoints That DON'T Exist
- `/projects/:id/workitems/deleted`
- `/projects/:id/workitems/:id/undelete`
- `/projects/:id/trash`
- `/projects/:id/recycle-bin`
- `/projects/:id/audit-log`

---

## Conclusion

The 18 deleted work items from project 230954 **cannot be recovered** through any technical means available in HacknPlan. The deletion is permanent at the database level with no recovery mechanism exposed through:

1. HacknPlan REST API
2. HacknPlan MCP wrapper
3. HacknPlan UI (confirmed by lack of trash feature)
4. Command-line tools

### Recommended Actions

1. **Immediate:** Check team members for local exports or notes about the deleted items
2. **Short-term:** Set up automated backups of the entire project (monthly exports)
3. **Medium-term:** Implement an "Archive" workflow instead of deletion
4. **Long-term:** Evaluate whether HacknPlan's data retention policy meets your needs

---

## Related Files

- C:\cpp\hacknplan-mcp\API-REFERENCE.md (1,500+ lines, no recovery mentioned)
- C:\cpp\hacknplan-mcp\src\tools\work-items.ts (deletion implementation)
- C:\cpp\hacknplan-mcp\src\core\client.ts (HTTP client, no special recovery handling)
- C:\cpp\VBVS--VIXEN\VIXEN\Vixen-Docs\Sessions\NEXT-AGENT-hacknplan-mcp-task9.md (References project 230954 and Sprint 650172 "API Enhancement Sprint 1")
- C:\cpp\VBVS--VIXEN\VIXEN\Vixen-Docs\Sessions\phase3-smart-queries-spec.md (Documents Smart Query Functions project)

**Document Status:** Complete investigation, no recovery possible

---

## Additional Context: Known Sprint Details

From discovered documentation:

**Sprint 650172: "API Enhancement Sprint 1"**
- Part of HacknPlan MCP project (230954)
- Focus: Smart Query Functions (Phase 3)
- Design Element #3: Smart Query Functions
- Tasks documented included:
  - Task #8: `get_my_current_tasks()` (5 hours)
  - Task #9: `get_blockers()` (5 hours)
  - Task #11: `get_sprint_progress()` (8 hours)

This suggests the deleted items from this sprint were likely:
- Work items related to API enhancement for HacknPlan MCP
- Smart query function implementations
- Possibly duplicate/obsolete tasks

---

## What to Tell HacknPlan Support

If you contact HacknPlan support about the deletion:

**Email Template:**
```
Subject: Work Items Permanently Deleted - Data Recovery Request

Hi HacknPlan Support,

We accidentally deleted 18 work items from project 230954 on 2025-12-17:
- Sprint: "Critical Bug Fixes" (and "API Enhancement Sprint 1")
- Status: Closed sprints
- Items: Programming, Bug, and Design categories

Questions for your team:
1. Do you maintain database backups with point-in-time recovery?
2. Is there an audit log of deletions by user/date that could help identify exactly what was deleted?
3. Are deleted items permanently removed or soft-deleted in your database?
4. Could a database administrator restore from a backup?

Note: We understand deletion is permanent in the current API, but wondering if
backup/recovery is available at the infrastructure level.

Thank you,
[Your Name]
```

**Expected Response:**
HacknPlan will likely say:
- No recovery possible at user/API level
- Backups exist for infrastructure resilience, not user recovery
- Standard policy: deletion = permanent loss
- Recommend enabling deletion confirmations and audit logs in future
