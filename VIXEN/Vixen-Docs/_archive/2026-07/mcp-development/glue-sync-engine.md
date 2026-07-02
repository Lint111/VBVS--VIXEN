---
status: in-progress
hacknplan_project: 230955
hacknplan_task: 4
tags: [mcp, sync-engine, obsidian-integration, hacknplan-integration]
created: 2025-12-16
---

# [Sync Engine] Bidirectional Vault-HacknPlan Sync

## Overview
Bidirectional synchronization system between Obsidian vault markdown files and HacknPlan design elements. Replaces the current TODO-list approach with actual atomic sync operations.

## HacknPlan Reference
- **Project**: 230955 (HacknPlan-Obsidian Glue MCP)
- **Work Item**: [#4](hacknplan://task/230955/4)
- **Sprint**: Sprint 1: Core Sync Engine
- **Estimate**: 80 hours

## Architecture

### Core Components

#### 1. File Watcher (chokidar)
- Monitor vault directory for file changes
- Debounce rapid changes (5-second batch window)
- Filter for `.md` files only
- Emit events: `file:created`, `file:modified`, `file:deleted`

#### 2. Sync Engine
- Process batched file events every 5 seconds
- Determine sync direction (vault→HP or HP→vault)
- Execute atomic operations
- Handle rollback on partial failures

#### 3. Frontmatter Parser
- Extract `hacknplan_id` from markdown frontmatter
- Update frontmatter when creating HacknPlan design elements
- Preserve existing frontmatter fields

#### 4. Conflict Detector
- Compare vault file `mtime` vs HacknPlan `updatedAt`
- Detect simultaneous edits (conflict window: <60 seconds)
- Resolution strategies:
  - Vault wins (default for local changes)
  - HacknPlan wins (for remote sync)
  - Manual merge (conflict detected)

#### 5. Atomic Operations
- Batch HacknPlan API calls
- All-or-nothing transaction semantics
- Rollback on any failure

## Current Codebase

**Monolith**: `C:\cpp\hacknplan-obsidian-glue\src\index.js` (744 lines)

**Needs Refactoring**:
- `src/syncEngine.js` - Core sync logic
- `src/fileWatcher.js` - chokidar integration
- `src/frontmatterParser.js` - YAML parsing
- `src/conflictDetector.js` - Timestamp comparison
- `src/operations.js` - Atomic HacknPlan operations

## Critical Issues Discovered (Architecture Review)

### [BLOCKER] No Sync Execution
**Location**: `index.js:527-572`
- `sync_vault_to_hacknplan` returns TODO lists, doesn't execute
- Requires N LLM round-trips (2-5 sec each)
- Makes automation impossible
- **Fix**: Add HacknPlan HTTP client, execute operations directly

### [BLOCKER] No HacknPlan API Client
- Glue MCP cannot call HacknPlan API
- No atomic transactions, batching, error recovery
- **Fix**: Add HTTP client with credentials or use MCP-to-MCP calls

### [MAJOR] Naive Frontmatter Parsing
**Location**: `index.js:114-129`
- Fails on YAML arrays, multi-line values, nested objects
- 30-50% of complex frontmatter will break
- **Fix**: Replace with `gray-matter` library

### [MAJOR] No Conflict Detection
**Location**: `index.js:527-641`
- Blindly overwrites without timestamp comparison
- Data loss risk on simultaneous edits
- **Fix**: Store `synced_at`, compare before write

### [MAJOR] No File Watching
- Sync is manual-only (no chokidar integration)
- **Fix**: Add chokidar with debouncing

### [MAJOR] Blocking Synchronous I/O
**Location**: `index.js:78-112`
- `readFileSync` blocks event loop (~10 sec for 1000 docs)
- **Fix**: Use `fs.promises.readFile` with `p-limit`

## Implementation Plan

### Phase 0: Critical Dependencies (COMPLETE ✅)
**Subtask: Add HacknPlan HTTP Client + TypeScript Migration**
- [x] Add `node-fetch` or use built-in `fetch` - Using Node.js 22 built-in fetch
- [x] Create `hacknplan-client.js` module
- [x] Read API key from environment variable
- [x] Implement API methods: `createDesignElement`, `updateDesignElement`, `getDesignElement`, `deleteDesignElement`, `listDesignElements`, `listDesignElementTypes`, `listTags`, `getProject`, `listProjects`, `listWorkItems`, `getWorkItem`
- [x] Add error handling and retry logic (exponential backoff for 429/5xx)
- [x] Test with HacknPlan project 230955 - 10/10 tests passing
- [x] **BONUS: Migrate entire codebase to TypeScript**
  - Converted 759-line JS monolith to modular TypeScript
  - Structure: `src/core/`, `src/lib/`, `src/tools/`
  - Added type definitions, Zod validation (future)
  - Build system: `npm run build`, `npm run dev`
  - All 12 MCP tools working in TypeScript
  - Version bumped to 2.0.0

**Actual**: 4 hours (1.5h HTTP client + 2.5h TypeScript migration) | **Risk**: Medium | **Dependencies**: None

**Key Discovery**: HacknPlan DELETE API requires empty JSON body `{}` with Content-Type header (not documented in Swagger)

### Phase 1: Fix Frontmatter Parsing (MAJOR - 2h)
**Subtask: Replace naive parser with gray-matter**
- [ ] Install `gray-matter` dependency
- [ ] Extract `frontmatter.js` module (lines 114-140)
- [ ] Replace manual regex parsing with `gray-matter.parse()`
- [ ] Add `gray-matter.stringify()` for writing frontmatter
- [ ] Test with complex YAML (arrays, nested objects)
- [ ] Handle missing frontmatter gracefully

**Estimate**: 2 hours | **Risk**: Low | **Dependencies**: None

### Phase 2: Async Vault Scanner (MAJOR - 3h)
**Subtask: Convert blocking I/O to async**
- [ ] Extract `vault-scanner.js` module (lines 78-112)
- [ ] Replace `readFileSync` with `fs.promises.readFile`
- [ ] Add `p-limit` for controlled concurrency (max 10 parallel reads)
- [ ] Update MCP tool handlers to use async scanner
- [ ] Test with large vault (1000+ files)

**Estimate**: 3 hours | **Risk**: Medium | **Dependencies**: Phase 1

### Phase 3: Sync State Tracking (MAJOR - 3h)
**Subtask: Add timestamp tracking for conflict detection**
- [ ] Create `sync-state.js` module
- [ ] Store `{ filePath: { lastSynced, vaultMtime, hacknplanUpdatedAt } }`
- [ ] Persist state to `.sync-state.json` in config directory
- [ ] Add `getSyncState()`, `updateSyncState()`, `clearSyncState()` methods
- [ ] Load state on MCP server startup

**Estimate**: 3 hours | **Risk**: Low | **Dependencies**: None

### Phase 4: Conflict Detection (HIGH PRIORITY - 6h)
**Subtask: Detect simultaneous edits**
- [ ] Create `conflict-resolver.js` module
- [ ] Implement 3-way comparison: (sync-state, vault mtime, HacknPlan updatedAt)
- [ ] Define conflict window: vault and HacknPlan both modified since last sync
- [ ] Add conflict resolution strategies:
   - **Vault wins**: Default for vault→HP sync
   - **HacknPlan wins**: Default for HP→vault sync
   - **Manual merge**: Surface conflict to user
- [ ] Add `diff` library for content comparison
- [ ] Return conflict metadata to user

**Estimate**: 6 hours | **Risk**: High | **Dependencies**: Phase 3

### Phase 5: Execute Real Sync (BLOCKER - 6h)
**Subtask: Make sync_vault_to_hacknplan execute operations**
- [ ] Refactor `sync_vault_to_hacknplan` (lines 527-572)
- [ ] For each vault document:
   - Check for conflicts (Phase 4)
   - If no conflict: call `hacknplanClient.createDesignElement()`
   - Update frontmatter with `hacknplan_id`
   - Update sync state
- [ ] Implement atomic transactions (all-or-nothing)
- [ ] Add rollback on partial failures
- [ ] Return summary: `{ created: N, updated: M, conflicts: K }`

**Estimate**: 6 hours | **Risk**: High | **Dependencies**: Phase 0, 1, 2, 3, 4

### Phase 6: File Watcher (4h)
**Subtask: Add chokidar for real-time sync**
- [ ] Install `chokidar` and `lodash.debounce` dependencies
- [ ] Create `file-watcher.js` module
- [ ] Initialize chokidar with vault path
- [ ] Filter for `.md` files only
- [ ] Debounce changes (5-second window)
- [ ] Emit events: `file:created`, `file:modified`, `file:deleted`
- [ ] Add MCP tools: `start_file_watcher`, `stop_file_watcher`, `get_sync_status`

**Estimate**: 4 hours | **Risk**: Medium | **Dependencies**: Phase 5

### Phase 7: Sync Queue (4h)
**Subtask: Batch operations for performance**
- [ ] Install `p-queue` dependency
- [ ] Create `sync-queue.js` module
- [ ] Queue file changes from watcher
- [ ] Process queue every 5 seconds (configurable)
- [ ] Batch HacknPlan API calls (max 10 per batch)
- [ ] Add retry logic for failed operations
- [ ] Track queue depth and processing time

**Estimate**: 4 hours | **Risk**: Medium | **Dependencies**: Phase 6

### Phase 8: Integration & Testing (6h)
**Subtask: End-to-end testing and cleanup**
- [ ] Wire all components together in main `index.js`
- [ ] Test full sync cycle: vault→HP→vault
- [ ] Test conflict scenarios (simultaneous edits)
- [ ] Test rollback on partial failures
- [ ] Test file watcher with rapid changes
- [ ] Add comprehensive error messages
- [ ] Update tool descriptions and examples

**Estimate**: 6 hours | **Risk**: Low | **Dependencies**: Phase 7

### Phase 9: Documentation & Handoff (2h)
**Subtask: Update documentation**
- [ ] Update README.md with new architecture
- [ ] Document new MCP tools (file watcher, sync status)
- [ ] Add conflict resolution guide
- [ ] Create troubleshooting section
- [ ] Update Obsidian vault docs with findings
- [ ] Create session summary for handoff

**Estimate**: 2 hours | **Risk**: Low | **Dependencies**: Phase 8

## Total Estimate: 40 hours

### Dependency Graph
```
Phase 0: HTTP Client (4h) ──┐
Phase 1: Frontmatter (2h) ───┼──┐
Phase 2: Async Scanner (3h) ─┤  │
Phase 3: Sync State (3h) ────┴──┼──> Phase 4: Conflicts (6h) ──┐
                                 │                               │
                                 └───────────────────────────────┼──> Phase 5: Real Sync (6h) ──> Phase 6: File Watcher (4h) ──> Phase 7: Sync Queue (4h) ──> Phase 8: Integration (6h) ──> Phase 9: Documentation (2h)
```

### Risk Assessment
- **High Risk**: Conflict detection (complex logic), Real sync execution (atomic operations)
- **Medium Risk**: HTTP client (API integration), Async scanner (concurrency), File watcher (chokidar integration), Sync queue (batching)
- **Low Risk**: Frontmatter parsing (library integration), Sync state (simple storage), Integration testing, Documentation

## Related Documentation
- [[03-Research/mcp-servers]] - MCP architecture overview
- [[01-Architecture/project-management-integration]] - PM tool integration patterns

## Progress Log

### 2025-12-16 - Phase 0 Complete: HacknPlan HTTP Client
- **Completed**: `src/hacknplan-client.js` module (290 lines)
- **Test Suite**: `src/test-hacknplan-client.js` - 10/10 tests passing
- **Key Discovery**: HacknPlan DELETE API requires empty body `{}` with Content-Type header
- **Actual Time**: 1.5 hours (estimated 4h)

**Implementation Details**:
- Uses Node.js 22+ built-in `fetch` (no dependencies)
- `HacknPlanClient` class with retry logic
- `HacknPlanAPIError` custom error with status code
- Exponential backoff for 429 (rate limit) and 5xx errors
- 60-second timeout using AbortController
- Full CRUD for design elements, plus read for tags/projects/work items

**Files Created**:
- `C:\cpp\hacknplan-obsidian-glue\src\hacknplan-client.js`
- `C:\cpp\hacknplan-obsidian-glue\src\test-hacknplan-client.js`

**package.json Updated**:
- Added `test` script: `npm test` runs client tests

### 2025-12-16 - Project Setup
- Created HacknPlan task #4 (80h estimate)
- Assigned to Lior Yaari
- Added to Sprint 1: Core Sync Engine
- Created this design document
