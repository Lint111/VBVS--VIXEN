# Dependency Fallback Parser Design

**Feature**: Premium-Free Tier Compatibility for Dependency Analysis
**Issue**: HacknPlan dependencies are a premium feature
**Solution**: Parse structured markers from task descriptions as fallback
**Status**: Specification (Task #9)

---

## Problem Statement

HacknPlan's dependency tracking (`GET /workitems/{id}/dependencies`) is only available on premium plans. Free-tier users cannot:
- Track task dependencies programmatically
- Identify blocking work items
- Generate critical path analysis

**Without a workaround, `get_blockers()` would be premium-only.**

---

## Solution: Dual-Mode Operation

### Mode 1: Premium API (Preferred)
```typescript
// Try premium API first
try {
  const deps = await ctx.httpRequest(
    `/projects/${projectId}/workitems/${workItemId}/dependencies`
  );
  return buildGraphFromAPI(deps); // source: 'api'
} catch (error) {
  if (error.status === 403 || error.status === 404) {
    // Fall back to description parser
  }
}
```

### Mode 2: Description Parser (Fallback)
```typescript
// Parse all work item descriptions for dependency markers
const items = await ctx.httpRequest(`/projects/${projectId}/workitems`);
const graph = parseDependencyMarkers(items);
return graph; // source: 'description-markers'
```

---

## Supported Marker Patterns

Users add structured markers to task descriptions:

### Pattern 1: "Blocked by"
```markdown
# Task #123: Implement shader system

Blocked by: #101, #102

This task requires device initialization (#101) and resource
management (#102) to be completed first.
```

**Regex:** `/Blocked by:?\s*#(\d+(?:\s*,\s*#\d+)*)/gi`

### Pattern 2: "Depends on"
```markdown
# Task #456: Add ray marching

Depends on #123

Needs the shader system implemented first.
```

**Regex:** `/Depends on:?\s*#(\d+(?:\s*,\s*#\d+)*)/gi`

### Pattern 3: "Prerequisites"
```markdown
# Task #789: Optimize rendering

Prerequisites: #456, #123

Must have ray marching and shaders working.
```

**Regex:** `/Prerequisites?:?\s*#(\d+(?:\s*,\s*#\d+)*)/gi`

### Pattern 4: "Requires"
```markdown
# Task #999: Profile performance

Requires: #789

Optimization must be complete before profiling.
```

**Regex:** `/Requires?:?\s*#(\d+(?:\s*,\s*#\d+)*)/gi`

---

## Parser Implementation

### Algorithm

```typescript
function parseDependencyMarkers(workItems: WorkItem[]): DependencyGraph {
  const graph = new Map<number, number[]>();

  // Regex patterns for all supported markers
  const patterns = [
    /Blocked by:?\s*#(\d+(?:\s*,\s*#\d+)*)/gi,
    /Depends on:?\s*#(\d+(?:\s*,\s*#\d+)*)/gi,
    /Prerequisites?:?\s*#(\d+(?:\s*,\s*#\d+)*)/gi,
    /Requires?:?\s*#(\d+(?:\s*,\s*#\d+)*)/gi,
  ];

  for (const item of workItems) {
    const description = item.description || '';
    const dependencies = new Set<number>();

    // Apply all patterns
    for (const pattern of patterns) {
      const matches = description.matchAll(pattern);
      for (const match of matches) {
        // Extract comma-separated task IDs
        const ids = match[1]
          .split(/\s*,\s*/)
          .map(s => parseInt(s.replace('#', ''), 10))
          .filter(id => !isNaN(id) && id > 0);

        ids.forEach(id => dependencies.add(id));
      }
    }

    if (dependencies.size > 0) {
      graph.set(item.workItemId, Array.from(dependencies));
    }
  }

  return graph;
}
```

### Validation

```typescript
function validateDependencies(
  graph: DependencyGraph,
  allWorkItems: WorkItem[]
): DependencyGraph {
  const validIds = new Set(allWorkItems.map(item => item.workItemId));
  const validated = new Map<number, number[]>();

  for (const [itemId, deps] of graph.entries()) {
    // Filter out invalid/non-existent task IDs
    const validDeps = deps.filter(depId => validIds.has(depId));

    if (validDeps.length > 0) {
      validated.set(itemId, validDeps);
    }
  }

  return validated;
}
```

---

## Error Handling

### Scenario 1: Malformed Markers
```markdown
Blocked by #abc, #456  // #abc is invalid
```

**Handling:** Skip invalid IDs, keep valid ones (#456)

### Scenario 2: Non-Existent Task IDs
```markdown
Blocked by: #999999  // Task doesn't exist
```

**Handling:** Validate against project work items, skip non-existent IDs

### Scenario 3: No Markers Found
```markdown
This task has no dependency markers at all.
```

**Handling:** Return empty dependency set (not an error)

### Scenario 4: Circular Dependencies
```markdown
Task #10: Blocked by #11
Task #11: Blocked by #10
```

**Handling:** Detect cycles during graph traversal, break infinite loops

---

## API vs. Parser Comparison

| Feature | Premium API | Description Parser |
|---------|-------------|-------------------|
| **Availability** | Premium plans only | All plans |
| **Performance** | Fast (dedicated endpoint) | Slower (fetch all items) |
| **Accuracy** | 100% accurate | Depends on marker consistency |
| **Maintenance** | None (official API) | Users must maintain markers |
| **Reverse Lookup** | Successors available | Must infer from graph |
| **Error Handling** | API validation | Manual validation needed |

---

## User Guidelines (Documentation)

### For Free-Tier Users

**Add dependency markers to task descriptions:**

1. **Choose a marker style** (pick one and stick to it):
   - `Blocked by: #123, #456`
   - `Depends on: #123, #456`
   - `Prerequisites: #123, #456`
   - `Requires: #123, #456`

2. **Use consistent formatting:**
   - Always use `#` prefix for task IDs
   - Comma-separate multiple dependencies
   - Place markers on their own line for clarity

3. **Keep markers updated:**
   - Remove markers when dependencies complete
   - Add new markers as dependencies emerge

### For Premium Users

**No action required** - the MCP will automatically use the dependency API.

---

## Configuration Options

```typescript
interface GetBlockersArgs {
  projectId?: number;
  workItemId?: number | string;
  includeIndirect?: boolean;

  // NEW: Fallback control
  useFallbackParser?: boolean;  // Default: true
}
```

**Modes:**
- `useFallbackParser: true` (default)
  - Try API first
  - Fall back to parser on 403/404
  - Best for mixed user bases

- `useFallbackParser: false`
  - Premium-only mode
  - Fail if API unavailable
  - Best for enterprise/premium-only setups

---

## Response Format

```typescript
interface GetBlockersResult {
  blocked: WorkItem[];      // Items with incomplete dependencies
  blockers: WorkItem[];     // Items blocking others
  criticalPath?: {          // If workItemId specified
    item: WorkItem;
    prerequisites: WorkItem[];
    depth: number;
  };
  source: 'api' | 'description-markers';  // NEW: How deps were detected
}
```

**Why include `source`?**
- Transparency: Users know which method was used
- Debugging: Helps diagnose parser issues
- UX: Can prompt free users to upgrade for better accuracy

---

## Performance Considerations

### API Mode (Premium)
- **Cost:** 1 API call per work item
- **Time:** ~50-100ms per item
- **Scale:** Excellent (dedicated endpoint)

### Parser Mode (Free)
- **Cost:** 1 API call to fetch all work items
- **Time:** ~200ms + parsing (all items)
- **Scale:** Good for <500 items, slower for >1000 items

**Optimization:** Cache work items for multiple parser calls:
```typescript
let cachedWorkItems: { items: WorkItem[], timestamp: number } | null = null;

async function getWorkItemsWithCache(projectId: number) {
  const now = Date.now();
  const CACHE_TTL = 60000; // 1 minute

  if (cachedWorkItems && (now - cachedWorkItems.timestamp) < CACHE_TTL) {
    return cachedWorkItems.items;
  }

  const items = await ctx.httpRequest(`/projects/${projectId}/workitems`);
  cachedWorkItems = { items, timestamp: now };
  return items;
}
```

---

## Testing Strategy

### Unit Tests

**API Mode:**
```typescript
test('get_blockers - API mode basic chain', async () => {
  // Mock API returning dependencies
  context.httpRequest = mockDependencyAPI({
    10: { predecessors: [{ workItemId: 11, isCompleted: false }] }
  });

  const result = await getBlockers({ projectId: 123 }, context);

  assert.strictEqual(result.source, 'api');
  assert.strictEqual(result.blocked.length, 1);
});
```

**Fallback Mode:**
```typescript
test('get_blockers - Parser mode with Blocked by marker', async () => {
  // Mock API throwing 403 (premium required)
  context.httpRequest = mockAPI403ThenReturnItems([
    { workItemId: 10, description: 'Blocked by: #11', isCompleted: false },
    { workItemId: 11, description: '', isCompleted: false }
  ]);

  const result = await getBlockers({ projectId: 123 }, context);

  assert.strictEqual(result.source, 'description-markers');
  assert.strictEqual(result.blocked.length, 1);
  assert.strictEqual(result.blocked[0].workItemId, 10);
});
```

**Mixed Markers:**
```typescript
test('get_blockers - Parser handles mixed marker styles', async () => {
  context.httpRequest = mockItems([
    { id: 1, description: 'Blocked by: #2, #3' },
    { id: 4, description: 'Depends on #5' },
    { id: 6, description: 'Prerequisites: #7\nRequires: #8' }
  ]);

  const graph = parseDependencyMarkers(items);

  assert.deepStrictEqual(graph.get(1), [2, 3]);
  assert.deepStrictEqual(graph.get(4), [5]);
  assert.deepStrictEqual(graph.get(6), [7, 8]);
});
```

**Malformed Markers:**
```typescript
test('get_blockers - Parser gracefully handles malformed markers', async () => {
  context.httpRequest = mockItems([
    { id: 1, description: 'Blocked by #abc, #456' },  // #abc invalid
    { id: 2, description: 'Blocked by: #999999' },    // Non-existent
    { id: 3, description: 'Blocked by: #456, #456' }, // Duplicate
  ]);

  const graph = parseDependencyMarkers(items);

  assert.deepStrictEqual(graph.get(1), [456]); // Skipped #abc
  assert.strictEqual(graph.get(2), undefined); // Skipped #999999 after validation
  assert.deepStrictEqual(graph.get(3), [456]); // Deduplicated
});
```

---

## Future Enhancements

### 1. Bidirectional Markers
Currently: Parser only detects "this task depends on X"
Future: Support "this task blocks X" for reverse relationships

```markdown
# Task #101: Device initialization
Blocks: #123, #456

This is a prerequisite for shader system (#123) and ray marching (#456).
```

### 2. Conditional Dependencies
```markdown
Blocked by: #123 (if using Vulkan backend)
```

### 3. External Dependencies
```markdown
Blocked by: External API approval
Depends on: Team Alpha's PR #789
```

---

## Decision Log

**Why regex instead of structured format (JSON/YAML)?**
- Users already write markdown descriptions
- Regex is non-invasive (doesn't require format change)
- Backward compatible with existing descriptions

**Why multiple marker patterns?**
- Different teams use different terminology
- Increases adoption (users pick what feels natural)
- Minimal implementation cost (just more regex)

**Why default `useFallbackParser: true`?**
- Most HacknPlan users are on free tier
- Graceful degradation > hard failure
- Premium users unaffected (API takes precedence)

**Why not write dependencies back to descriptions from API?**
- Avoids description pollution
- Users may have carefully crafted descriptions
- One-way sync (API → parser) is cleaner

---

## Related Tasks

- **Task #9**: Implement `get_blockers()` with fallback (this document)
- **Future**: Document fallback parser in user-facing docs
- **Future**: Add `set_dependency_marker()` helper for free users

---

**Status:** Ready for implementation (Task #9)
**Last Updated:** 2025-12-16 by Claude Sonnet 4.5
