# Sandbox Permissions

## Destructive Action Classification

Actions requiring **explicit user confirmation** before execution:

| Category | Operations |
|----------|------------|
| **File System** | `delete`, `overwrite`, `move` (outside temp/) |
| **Git** | `push`, `force-push`, `reset --hard`, `branch -D` |
| **External APIs** | `POST`, `PUT`, `DELETE` to production endpoints |
| **Database** | `DROP`, `TRUNCATE`, `DELETE` without WHERE |
| **HacknPlan** | Batch deletions, sprint modifications |

## Confirmation Flow

```
Script requests destructive operation
       ↓
Sandbox PAUSES execution
       ↓
Surfaces to Claude Code:
  "Script wants to: [operation description]
   Affected: [list of targets]
   Confirm? [Y/N]"
       ↓
User confirms → execution continues
User denies → script receives rejection, must handle gracefully
```

## Implementation Pattern

Scripts must use gated wrappers for destructive ops:

```typescript
// WRONG - direct destructive call
await fs.rm(filePath);

// RIGHT - gated through confirmation
await sandbox.requestDestructive({
  operation: 'delete',
  targets: [filePath],
  reason: 'Cleaning up temporary build artifacts'
});
```

## Safe Operations (No Confirmation Required)

- Read operations (file read, API GET, database SELECT)
- Writes to `temp/` directory
- Creating new files (non-overwrite)
- Logging and archival operations

## Escape Hatch

For batch operations where per-item confirmation is impractical:

```typescript
await sandbox.requestBatchDestructive({
  operation: 'delete',
  targets: files,  // Array of 50+ items
  summary: 'Delete 57 stale cache files from build/',
  showSample: 5    // Show first 5 in confirmation prompt
});
```

User sees: "Delete 57 files (showing 5): file1, file2, ... Confirm all? [Y/N]"
