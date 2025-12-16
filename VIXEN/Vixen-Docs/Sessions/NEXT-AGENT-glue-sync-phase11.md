# 🚀 Next Agent: Glue-Sync-Engine Phase 11

**Project:** HacknPlan-Obsidian Glue MCP (ID: 230955)
**Date:** 2025-12-16
**Previous Session:** Phase 9-10 Complete (Documentation + Test Infrastructure)
**Current Status:** Phase 11 Implementation - Core modules DONE, Tests PENDING

---

## What Just Happened

✅ **Phase 9 & 10 Complete** (Commit: `50f3905`)
- 3400+ lines of documentation
- Jest configuration and test infrastructure
- Test fixtures created
- 100-test plan defined

✅ **Core Implementation Complete**
- All 9 library modules implemented in `src/lib/`:
  - `conflict-resolver.ts` - 3-way conflict detection
  - `file-watcher.ts` - Chokidar real-time watching
  - `frontmatter.ts` - gray-matter parser
  - `single-file-sync.ts` - Optimized sync (56-112ms)
  - `sync-executor.ts` - Real sync execution
  - `sync-queue.ts` - Batched queue with retry
  - `sync-state.ts` - Persistent state tracking
  - `sync.ts` - Bidirectional sync engine
  - `vault-scanner.ts` - Async vault scanning

✅ **All 12 MCP Tools Implemented** in `src/tools/`:
- Pairing management (5 tools)
- Vault operations (2 tools)
- Sync operations (2 tools)
- Cross-reference (3 tools)

✅ **Build Status:**
- TypeScript compilation: ✅ PASS (no errors)
- Repository: Clean working tree

---

## What You Need to Do Next

### Your Main Task: Install Dependencies & Write Tests

**Phase 11 Remaining Work:**
1. Install test dependencies
2. Write 100+ tests (60% unit, 30% integration, 10% E2E)
3. Achieve 80%+ coverage
4. Manual testing with real vault
5. Update HacknPlan Task #4

---

## Step-by-Step Instructions

### 1. Install Test Dependencies

```bash
cd /c/cpp/hacknplan-obsidian-glue
npm install --save-dev jest@^29.7.0 ts-jest@^29.1.0 @types/jest@^29.5.0 mock-fs@^5.2.0 @faker-js/faker@^8.4.0
```

Verify jest is available:
```bash
npm test -- --version
```

---

### 2. Write Unit Tests (60% - ~60 tests)

**Priority Modules to Test:**

#### A. `src/lib/__tests__/frontmatter.test.ts`
```typescript
import { extractFrontmatter, updateFrontmatter } from '../frontmatter';

describe('Frontmatter', () => {
  test('extracts valid YAML frontmatter', () => {
    const content = `---
title: Test Doc
tags: [vulkan, rendering]
---
# Content`;

    const fm = extractFrontmatter(content);
    expect(fm.title).toBe('Test Doc');
    expect(fm.tags).toEqual(['vulkan', 'rendering']);
  });

  test('handles missing frontmatter gracefully', () => {
    const result = extractFrontmatter('# Just content');
    expect(result).toEqual({});
  });

  // Add 6-8 more test cases:
  // - Malformed YAML
  // - Empty frontmatter
  // - Update frontmatter
  // - Preserve content
});
```

#### B. `src/lib/__tests__/sync-state.test.ts`
```typescript
import { SyncStateManager } from '../sync-state';
import * as fs from 'fs/promises';

describe('SyncState', () => {
  let stateManager: SyncStateManager;
  const testDir = '/tmp/test-sync-state';

  beforeEach(async () => {
    await fs.mkdir(testDir, { recursive: true });
    stateManager = new SyncStateManager(testDir);
  });

  afterEach(async () => {
    await fs.rm(testDir, { recursive: true, force: true });
  });

  test('creates new state file if missing', async () => {
    const state = await stateManager.getState(123);
    expect(state.projectId).toBe(123);
    expect(state.documents).toEqual({});
  });

  test('persists state to disk', async () => {
    const state = await stateManager.getState(123);
    state.documents['Architecture/System.md'] = {
      designElementId: 456,
      hash: 'abc123',
      lastSyncTime: Date.now()
    };
    await stateManager.saveState(state);

    // Verify file exists
    const file = `${testDir}/sync-state-123.json`;
    const exists = await fs.access(file).then(() => true).catch(() => false);
    expect(exists).toBe(true);
  });

  // Add 6-8 more test cases
});
```

#### C. `src/lib/__tests__/conflict-resolver.test.ts`
#### D. `src/lib/__tests__/single-file-sync.test.ts`
#### E. `src/lib/__tests__/sync-queue.test.ts`
#### F. `src/lib/__tests__/vault-scanner.test.ts`

**Goal:** 8-10 tests per module = 60 unit tests total

---

### 3. Write Integration Tests (30% - ~30 tests)

**Location:** `tests/integration/`

#### Example: File Watcher + Sync Queue
```typescript
import { FileWatcher } from '../../src/lib/file-watcher';
import { SyncQueue } from '../../src/lib/sync-queue';
import * as fs from 'fs/promises';

describe('FileWatcher + SyncQueue Integration', () => {
  let watcher: FileWatcher;
  let queue: SyncQueue;
  const testVault = '/tmp/test-vault';

  beforeEach(async () => {
    await fs.mkdir(testVault, { recursive: true });
    watcher = new FileWatcher();
    queue = new SyncQueue(mockHacknPlanClient, mockPairingManager);
  });

  test('detects file changes and queues sync', async () => {
    await watcher.start(testVault);

    // Listen for changes
    const changes = [];
    watcher.on('changes-ready', (c) => changes.push(...c));

    // Create a file
    await fs.writeFile(`${testVault}/test.md`, '# Test');

    // Wait for debounce (1s)
    await delay(1500);

    expect(changes.length).toBe(1);
    expect(changes[0].event).toBe('add');
  });

  // Add 8-10 more integration tests
});
```

**Test Scenarios:**
- Vault scan → sync operation generation
- File change → queue → execution
- Conflict detection → resolution
- Tag extraction → mapping → assignment

---

### 4. Write E2E Tests (10% - ~10 tests)

**Location:** `tests/e2e/`

```typescript
describe('E2E: Full Sync Workflow', () => {
  test('syncs new vault document to HacknPlan', async () => {
    // 1. Create pairing
    const pairing = await addPairing({...});

    // 2. Create vault document
    await fs.writeFile(`${vaultPath}/Architecture/System.md`, content);

    // 3. Trigger sync
    const result = await syncVaultToHacknPlan(pairing.projectId);

    // 4. Verify design element created
    expect(result.created).toBe(1);

    // 5. Check HacknPlan API
    const elements = await hacknplanClient.listDesignElements(projectId);
    expect(elements.find(e => e.name === 'System')).toBeDefined();
  });

  // Add 9 more E2E tests
});
```

---

### 5. Run Tests and Check Coverage

```bash
# Run all tests
npm test

# Run with coverage
npm run test:coverage

# Run specific test suites
npm run test:unit
npm run test:integration
npm run test:e2e
```

**Coverage Goals:** 80%+ across:
- Statements
- Branches
- Functions
- Lines

---

### 6. Manual Testing

Create a test vault and pairing:
```bash
# Set environment
set HACKNPLAN_API_KEY=your_key_here

# Start MCP server
npm run dev

# Use Claude Desktop to test:
# - add_pairing
# - scan_vault
# - sync_vault_to_hacknplan
# - File watching (edit a vault doc, check queue)
```

---

### 7. Update HacknPlan Task #4

After tests complete:
```bash
# Use hacknplan-manager agent to:
# - Log hours (estimate remaining work)
# - Add comment with test results
# - Update phase status
```

---

## Key Files to Know

| File | Status | Tests Needed |
|------|--------|--------------|
| `src/lib/frontmatter.ts` | ✅ Implemented | 8-10 unit tests |
| `src/lib/sync-state.ts` | ✅ Implemented | 8-10 unit tests |
| `src/lib/conflict-resolver.ts` | ✅ Implemented | 8-10 unit tests |
| `src/lib/single-file-sync.ts` | ✅ Implemented | 8-10 unit tests |
| `src/lib/sync-queue.ts` | ✅ Implemented | 8-10 unit tests |
| `src/lib/vault-scanner.ts` | ✅ Implemented | 8-10 unit tests |
| `src/lib/file-watcher.ts` | ✅ Implemented | Integration tests |
| `src/lib/sync-executor.ts` | ✅ Implemented | Integration tests |
| `src/lib/sync.ts` | ✅ Implemented | Integration tests |
| `src/tools/*.ts` | ✅ Implemented | E2E tests |

---

## Test Infrastructure Already Complete

✅ `jest.config.js` - Configured
✅ `tests/setup.ts` - Global utilities
✅ `tests/fixtures/` - Sample vault + config
✅ `tests/README.md` - Test guide
✅ `package.json` - Test scripts defined

**Just need to install dependencies and write the actual tests!**

---

## Success Criteria

- [ ] Jest dependencies installed
- [ ] 60+ unit tests written and passing
- [ ] 30+ integration tests written and passing
- [ ] 10+ E2E tests written and passing
- [ ] 80%+ coverage achieved
- [ ] Manual testing with real vault successful
- [ ] HacknPlan Task #4 updated with progress
- [ ] No TypeScript errors
- [ ] Clean commit with test suite

---

## HacknPlan Context

**Project:** 230955 (HacknPlan-Obsidian Glue MCP)
**Sprint:** 650153 - Sprint 1: Core Sync Engine
**Current Task:** #4 - Implement Real Sync Engine (6.5/80h logged)

**Completed Infrastructure (Items 9-17):**
- HTTP Client
- Frontmatter parser
- Async vault scanner
- Sync state tracking
- 3-way conflict detection
- Real sync execution
- File watcher
- Operation queue
- Integration testing

---

## Quick Commands

```bash
# Install dependencies
cd /c/cpp/hacknplan-obsidian-glue
npm install

# Build
npm run build

# Run tests
npm test

# Watch mode
npm run test:watch

# Coverage
npm run test:coverage

# Check git status
git status
```

---

## If You Get Stuck

**Reference Documentation:**
- `docs/TESTING.md` - Test strategy and examples
- `docs/ARCHITECTURE.md` - System internals
- `docs/API.md` - Module signatures

**Existing Test Examples:**
- `src/lib/__tests__/frontmatter.test.ts` - Basic structure
- `src/lib/__tests__/conflict-resolver.test.ts` - Test patterns

**Jest Resources:**
- https://jestjs.io/docs/getting-started
- https://github.com/kulshekhar/ts-jest

---

## Commit Message Template

```
test(glue): Complete Phase 11 test suite (100+ tests, 80%+ coverage)

Implement comprehensive test coverage for hacknplan-obsidian-glue MCP.

**Test Suite:**
- Unit Tests: 60+ tests across 6 modules
  - Frontmatter parsing
  - Sync state management
  - Conflict resolution
  - Single-file sync optimization
  - Sync queue with retry
  - Vault scanning
- Integration Tests: 30+ tests
  - FileWatcher + SyncQueue
  - Vault scan + sync generation
  - Tag extraction + mapping
  - End-to-end workflows
- E2E Tests: 10+ tests
  - Full sync workflows
  - Real-time file watching
  - Conflict scenarios

**Coverage:**
- Statements: XX%
- Branches: XX%
- Functions: XX%
- Lines: XX%

**Build Status:**
- TypeScript compilation: ✓ PASS
- All tests: ✓ XX/100+ PASS
- Coverage thresholds: ✓ MET

Task: #4 - Implement Real Sync Engine (Phase 11 complete)
Sprint: 650153 - Core Sync Engine
Project: HacknPlan-Obsidian Glue MCP (ID: 230955)

🤖 Generated with [Claude Code](https://claude.com/claude-code)

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>
```

---

**Good luck! The implementation is done. Just need the test suite.** 🚀

*Last updated: 2025-12-16 by Claude Sonnet 4.5 (Phase 11 test handoff)*
