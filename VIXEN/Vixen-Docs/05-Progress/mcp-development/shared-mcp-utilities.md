# Shared MCP Utilities Library

**Created**: 2025-12-16
**Status**: Design Phase
**Related**: [glue-sync-engine.md](glue-sync-engine.md)

## Overview

Both HacknPlan MCP and HacknPlan-Obsidian Glue MCP share similar architectural patterns. Extracting common utilities into a shared library would:
- Reduce code duplication
- Ensure consistency across projects
- Simplify maintenance and testing
- Enable faster feature development

---

## Shared Patterns Identified

### 1. **Tool Registry System** ⭐⭐⭐ HIGH PRIORITY

**Current Duplication:**
- `hacknplan-mcp/src/tools/registry.ts`
- `hacknplan-obsidian-glue/src/tools/registry.ts`

**Common Pattern:**
```typescript
export function createToolRegistry(tools: ToolDefinition[]): ToolRegistry {
  const registry: ToolRegistry = new Map();
  for (const tool of tools) {
    if (registry.has(tool.name)) {
      throw new Error(`Duplicate tool name: ${tool.name}`);
    }
    registry.set(tool.name, tool);
  }
  return registry;
}

export function getToolSchemas(registry: ToolRegistry): Array<...> {
  return Array.from(registry.values()).map((tool) => ({
    name: tool.name,
    description: tool.description,
    inputSchema: tool.inputSchema,
  }));
}

export async function executeTool<TResult>(
  registry: ToolRegistry,
  toolName: string,
  args: unknown,
  context: ToolContext
): Promise<TResult> {
  const tool = registry.get(toolName);
  if (!tool) throw new Error(`Unknown tool: ${toolName}`);
  return (await tool.handler(args, context)) as TResult;
}
```

**Reusability**: 100% - Identical implementation in both projects

---

### 2. **Type Definitions** ⭐⭐⭐ HIGH PRIORITY

**Common Types:**
```typescript
export interface ToolDefinition<TArgs = any, TResult = unknown> {
  name: string;
  description: string;
  inputSchema: {
    type: 'object';
    properties: Record<string, unknown>;
    required?: string[];
  };
  handler: ToolHandler<TArgs, TResult>;
}

export type ToolHandler<TArgs = unknown, TResult = unknown> = (
  args: TArgs,
  context: ToolContext
) => Promise<TResult>;

export type ToolRegistry = Map<string, ToolDefinition>;
```

**Reusability**: 95% - `ToolContext` is project-specific, rest is identical

---

### 3. **LRU Cache with TTL** ⭐⭐ MEDIUM PRIORITY

**Location**: `hacknplan-mcp/src/core/cache.ts`

**Features:**
- Generic type support: `LRUCache<T>`
- Automatic eviction of least-recently-used entries
- Time-to-live (TTL) expiration
- Max size enforcement

**Potential Use in Glue MCP:**
- Cache sync state lookups
- Cache HacknPlan metadata (categories, tags, stages)
- Cache vault scan results (short TTL)

**Code Quality**: ⭐⭐⭐⭐⭐
- Well-documented
- Type-safe
- Production-ready

---

### 4. **Pagination Utilities** ⭐⭐ MEDIUM PRIORITY

**Location**: `hacknplan-mcp/src/utils/pagination.ts`

**Pattern:**
```typescript
export function paginateResults<T>(
  items: T[] | { items: T[] },
  offset: number = 0,
  limit: number = 50
): PaginatedResponse<T> {
  const allItems = Array.isArray(items) ? items : (items.items || []);
  const paginatedItems = allItems.slice(offset, offset + limit);
  return {
    items: paginatedItems,
    total: allItems.length,
    offset,
    limit,
    hasMore: offset + limit < allItems.length
  };
}
```

**Potential Use in Glue MCP:**
- Paginate vault document lists
- Paginate sync queue stats
- Paginate failed operations

---

### 5. **Batch Operations** ⭐⭐ MEDIUM PRIORITY

**Location**: `hacknplan-mcp/src/utils/batch.ts`

**Features:**
- Parallel execution with `Promise.allSettled`
- Individual error isolation
- Success/failure tracking
- Item identification for debugging

**Pattern:**
```typescript
export async function batchOperation<TItem, TResult>(
  items: TItem[],
  operation: (item: TItem) => Promise<TResult>,
  itemIdentifier: string = 'item'
): Promise<BatchOperationResult<TResult>>
```

**Potential Use in Glue MCP:**
- Already using p-limit in SyncQueue - could enhance with batch utilities
- Batch vault document operations
- Batch HacknPlan API calls

---

### 6. **Name Resolution System** ⭐ LOW PRIORITY (project-specific)

**Location**: `hacknplan-mcp/src/utils/resolution.ts`

**Features:**
- Resolve tag names to IDs
- Resolve category names to IDs
- Resolve importance levels
- Resolve work item references

**Glue MCP Equivalent:**
- Already has `folderMappings` and `tagMappings` in Pairing
- Less complex resolution needs

**Reusability**: 30% - Too domain-specific to HacknPlan

---

## Proposed Shared Library

### Package Name
`@vixen/mcp-utils` or `mcp-server-toolkit`

### Structure
```
@vixen/mcp-utils/
├── src/
│   ├── registry/
│   │   ├── registry.ts          # Tool registry core
│   │   ├── types.ts              # ToolDefinition, ToolHandler, ToolRegistry
│   │   └── index.ts
│   ├── cache/
│   │   ├── lru-cache.ts          # LRU Cache implementation
│   │   ├── types.ts              # CacheEntry, etc.
│   │   └── index.ts
│   ├── pagination/
│   │   ├── paginate.ts           # Pagination utilities
│   │   ├── types.ts              # PaginatedResponse
│   │   └── index.ts
│   ├── batch/
│   │   ├── batch.ts              # Batch operation utilities
│   │   ├── types.ts              # BatchOperationResult
│   │   └── index.ts
│   └── index.ts                  # Barrel export
├── package.json
├── tsconfig.json
└── README.md
```

---

## Implementation Plan

### Phase 1: Extract Core Utilities (2h)
1. Create new npm package `@vixen/mcp-utils`
2. Extract tool registry system
3. Extract type definitions
4. Set up TypeScript build
5. Add comprehensive tests

### Phase 2: Extract Cache & Pagination (1h)
1. Extract LRU Cache with TTL
2. Extract pagination utilities
3. Add tests

### Phase 3: Integrate into HacknPlan MCP (1h)
1. Install `@vixen/mcp-utils` as dependency
2. Replace local implementations
3. Update imports
4. Verify all tests pass

### Phase 4: Integrate into Glue MCP (1h)
1. Install `@vixen/mcp-utils` as dependency
2. Replace tool registry
3. Add caching where beneficial
4. Verify build and tests

### Phase 5: Optional - Extract Batch Utilities (1h)
1. Extract batch operation utilities
2. Enhance SyncQueue with batch utilities
3. Tests and documentation

**Total Estimated Time**: 6 hours

---

## Benefits Analysis

### Code Reduction
- **HacknPlan MCP**: Remove ~300 lines (registry, types, cache, pagination, batch)
- **Glue MCP**: Remove ~150 lines (registry, types)
- **Shared Library**: +500 lines (with tests and docs)
- **Net Reduction**: ~450 lines across projects

### Maintenance
- ✅ Single source of truth for common patterns
- ✅ Bug fixes benefit both projects
- ✅ Features benefit both projects
- ✅ Easier to test in isolation

### Consistency
- ✅ Same tool registry behavior
- ✅ Same caching strategy
- ✅ Same pagination format
- ✅ Same error handling patterns

### Development Speed
- ✅ New MCP servers can reuse utilities
- ✅ No need to re-implement common patterns
- ✅ Focus on domain logic, not infrastructure

---

## Trade-offs

### Pros
- DRY principle (Don't Repeat Yourself)
- Easier to maintain and evolve
- Better testability
- Consistent behavior across projects
- Faster development of new MCP servers

### Cons
- Additional dependency to manage
- Need to version the shared library
- Breaking changes require coordinated updates
- Initial extraction effort (6 hours)

---

## Decision: Defer Until Post-Phase 9

**Recommendation**: Complete Phases 8-9 of Glue MCP first, then extract shared utilities.

**Reasoning:**
1. Glue MCP sync engine is nearly complete (34h/40h)
2. Extraction is a nice-to-have, not critical
3. Both projects are stable enough to extract safely
4. Post-completion extraction allows clear comparison

**Next Steps After Phase 9:**
1. Create `@vixen/mcp-utils` package
2. Extract and test utilities
3. Integrate into both projects
4. Document migration guide

---

## Comparison: HacknPlan MCP vs Glue MCP

| Aspect | HacknPlan MCP | Glue MCP |
|--------|---------------|----------|
| **Registry** | ✅ createToolRegistry, executeTool, getToolSchemas | ✅ Identical implementation |
| **Types** | ✅ ToolDefinition, ToolHandler, ToolRegistry | ✅ Identical (ToolContext differs) |
| **Cache** | ✅ LRUCache with TTL | ❌ Not implemented (could benefit) |
| **Pagination** | ✅ paginateResults | ❌ Not needed yet |
| **Batch Ops** | ✅ batchOperation | ⚠️ Uses p-limit directly in SyncQueue |
| **Resolution** | ✅ Name→ID resolution | ⚠️ Simple mappings in Pairing |
| **Transactions** | ✅ Transaction support | ❌ Uses rollback in executors |

---

## Example: Using Shared Library

### Before (Glue MCP)
```typescript
// src/tools/registry.ts (duplicate code)
export function createToolRegistry(tools: ToolDefinition[]): ToolRegistry {
  const registry: ToolRegistry = new Map();
  for (const tool of tools) {
    if (registry.has(tool.name)) {
      throw new Error(`Duplicate tool name: ${tool.name}`);
    }
    registry.set(tool.name, tool);
  }
  return registry;
}
```

### After (With Shared Library)
```typescript
// src/tools/registry.ts
import { createToolRegistry, getToolSchemas, executeTool } from '@vixen/mcp-utils';
import { pairingTools } from './pairing.js';
import { vaultTools } from './vault.js';
// ... other imports

export function createGlobalRegistry(): ToolRegistry {
  return createToolRegistry([
    ...pairingTools,
    ...vaultTools,
    // ...
  ]);
}
```

---

## References

**HacknPlan MCP Files:**
- `src/core/cache.ts` - LRU Cache implementation
- `src/utils/pagination.ts` - Pagination utilities
- `src/utils/batch.ts` - Batch operation utilities
- `src/tools/registry.ts` - Tool registry system
- `src/tools/types.ts` - Tool type definitions

**Glue MCP Files:**
- `src/tools/registry.ts` - Tool registry (duplicate)
- `src/tools/types.ts` - Tool types (duplicate)

---

## Status: DOCUMENTED

Ready for extraction after Phase 9 completion.

Next: Complete Phases 8-9, then revisit shared library extraction.
