# Script Archival

## Archive Structure

Every sandbox execution creates:

```
temp/sandbox/active/{timestamp}-{purpose}/
├── script.ts       # The executed code
├── context.json    # Metadata about intent
└── result.json     # Execution outcome
```

## Naming Convention

`{timestamp}-{purpose}` where:
- `timestamp`: `YYYYMMDD-HHmmss` (e.g., `20251228-143052`)
- `purpose`: kebab-case, max 40 chars (e.g., `fetch-sprint-tasks`)

## context.json Schema

```json
{
  "purpose": "Human-readable intent of this script",
  "triggeredBy": "User request or agent task that initiated this",
  "params": {
    "key": "value passed to script"
  },
  "mcpServersUsed": ["hacknplan"],
  "estimatedOperations": 5,
  "destructiveOps": false,
  "relatedTask": "HacknPlan task ID if applicable"
}
```

## result.json Schema

```json
{
  "status": "success | failure | partial",
  "startedAt": "ISO timestamp",
  "completedAt": "ISO timestamp",
  "durationMs": 1234,
  "output": {
    "summary": "Brief human-readable result",
    "data": {}
  },
  "errors": [],
  "operationsPerformed": 5,
  "confirmationsRequired": 0,
  "patternCandidate": false
}
```

## Pattern Promotion

When a script proves reusable:

1. Set `result.patternCandidate: true`
2. After task completion, review candidates
3. Promote to permanent location:
   - **Project-specific**: `memory-bank/sandbox-patterns/{category}/`
   - **Cross-project**: `~/.claude/sandbox-patterns/{category}/`

```typescript
/**
 * Pattern: fetch-active-sprint-summary
 * Category: hacknplan
 * Promoted: 2025-12-28
 *
 * Fetches current sprint and formats task summary.
 *
 * @param projectId - HacknPlan project ID
 * @returns Formatted sprint summary
 */
export async function fetchActiveSprintSummary(projectId: string) {
  // ... vetted implementation
}
```

## Cleanup Protocol

Archives cleared when:
1. User explicitly triggers cleanup (`/sandbox-cleanup` or similar)
2. Task reaches end state (feature complete, PR merged)
3. Session handoff (after session-summary generated)

**Never auto-delete** - user controls cleanup timing.

## Retention Exceptions

Keep in permanent pattern libraries:
- **memory-bank/sandbox-patterns/**: Project-specific reusable patterns
- **~/.claude/sandbox-patterns/**: Cross-project reusable patterns
- Scripts that uncovered bugs (for regression reference)
- Complex multi-MCP orchestrations (learning reference)
