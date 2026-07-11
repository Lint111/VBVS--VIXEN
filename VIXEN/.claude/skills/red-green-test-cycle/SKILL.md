---
name: red-green-test-cycle
description: Test-driven debugging workflow using Red-Yellow-Green cycle. Use when fixing failing tests, validating test relevance, adding test metadata, or systematically working through test suites. Focuses on one test at a time with minimal intrusive builds.
allowed-tools: Bash, Read, Edit, Write, Grep, Glob, Task
---

# Red-Yellow-Green Test Cycle

Systematic test-driven debugging workflow that processes tests one at a time through validation, metadata enrichment, and fixing phases.

## Workflow States

| State | Meaning | Action |
|-------|---------|--------|
| **RED** | Test failing | Debug and fix the code |
| **YELLOW** | Test needs review | Validate relevance, update metadata |
| **GREEN** | Test passing | Move to next test |

## Phase 1: Test Discovery & Triage

1. **Identify failing tests**
   ```bash
   # Run test suite, capture failures
   ./build/path/to/test.exe --gtest_brief=1 2>&1
   ```

2. **Create test queue** ordered by:
   - Dependencies (foundational tests first)
   - Complexity (simple before complex)
   - Related functionality groups

## Phase 2: Yellow Phase - Test Validation

For each test, before debugging:

### 2.1 Relevance Check
- Does this test still match current architecture?
- Is the tested functionality still needed?
- Is this test duplicating another?

**If test is obsolete:** Mark for removal, document reason, skip to next test.

### 2.2 Metadata Enrichment

Check/add these metadata elements to test file:

```cpp
// @coverage: SVORebuild::rebuildFromLeaves, SVONode::computeChildMask
// @category: unit | integration | performance
// @last-pass: 2024-12-01 (auto-updated on green)
// @compile-time-baseline: 2.3s (for performance regression detection)
// @dependencies: test_svo_basic must pass first
// @owner: SVO subsystem
```

### 2.3 Update Test Infrastructure
- Ensure test uses current APIs
- Update deprecated calls
- Fix include paths if architecture changed

## Phase 3: Red Phase - Debug & Fix

### 3.1 Isolate the Failure
```bash
# Run single test in isolation
./build/path/to/test.exe --gtest_filter=TestSuite.SpecificTest
```

### 3.2 Analyze Failure
- Read test output carefully
- Identify assertion that failed
- Trace to source code location

### 3.3 Minimal Fix Strategy
- Fix ONLY what's needed for this test
- No refactoring during red phase
- No "while I'm here" improvements

### 3.4 Focused Build
```bash
# Build only affected target
cmake --build build --config Debug --target <specific_target> --parallel 16
```

### 3.5 Verify Fix
```bash
# Re-run only the specific test
./build/path/to/test.exe --gtest_filter=TestSuite.SpecificTest --gtest_brief=1
```

## Phase 4: Green Phase - Validate & Document

### 4.1 Confirm Green
- Test passes consistently (run 2-3 times)
- No new warnings introduced

### 4.2 Update Metadata
```cpp
// @last-pass: <today's date>
```

### 4.3 Regression Check
```bash
# Run related tests to ensure fix didn't break others
./build/path/to/test.exe --gtest_brief=1
```

### 4.4 Move to Next Test
- Mark current test complete
- Pop next test from queue
- Return to Phase 2

## Build Optimization Rules

1. **Never full rebuild** during test cycle
2. **Target-specific builds** only:
   ```bash
   cmake --build build --config Debug --target SVO --parallel 16
   ```
3. **Skip unrelated tests** during iteration
4. **Batch metadata updates** (don't rebuild for comments)

## Progress Tracking

Maintain a test status file:

```markdown
## Test Cycle Progress - <date>

### Queue
- [ ] test_rebuild_hierarchy::BasicRebuild (RED)
- [ ] test_ray_casting::IntersectionAccuracy (YELLOW)
- [ ] test_lod::LODSelection (pending)

### Completed
- [x] test_svo_basic::NodeCreation (GREEN) - fixed null check
- [x] test_compression::DXT1 (REMOVED) - obsolete after refactor

### Blocked
- test_streaming::AsyncLoad - depends on test_io_basic
```

## When to Use This Skill

- After major refactoring with multiple test failures
- During CI/CD pipeline failure investigation
- Systematic test suite maintenance
- Pre-release quality assurance passes
- When bug-hunter or test-framework-qa agents need structured approach

## Integration with Agents

This skill is designed to work with:
- **bug-hunter**: Provides systematic debugging structure
- **test-framework-qa**: Provides metadata and coverage tracking
- **coding-partner**: For fix implementation guidance
