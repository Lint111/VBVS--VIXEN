# Red-Green-Test-Cycle Examples

## Example 1: Single Failing Test

**Scenario:** `test_rebuild_hierarchy::BasicRebuild` is failing after SVONode refactor.

### Yellow Phase
```
Agent: Checking test relevance...

Test: test_rebuild_hierarchy::BasicRebuild
File: libraries/SVO/tests/test_rebuild_hierarchy.cpp:45

Relevance: VALID - tests core rebuild functionality
Current metadata: MISSING

Adding metadata:
// @coverage SVORebuild::rebuildFromLeaves
// @category unit
// @owner SVO
```

### Red Phase
```
Agent: Running isolated test...

$ ./build/libraries/SVO/tests/Debug/test_rebuild_hierarchy.exe \
    --gtest_filter=SVORebuildTest.BasicRebuild

FAILED: Expected childMask 0xFF, got 0x00
  at SVORebuild.cpp:156

Agent: Analyzing failure...
- rebuildFromLeaves() not propagating child masks
- Root cause: missing computeChildMask() call after leaf insertion

Agent: Applying minimal fix...
Edit: SVORebuild.cpp:156
+ node->computeChildMask();

Agent: Building target only...
$ cmake --build build --config Debug --target SVO --parallel 16
Build time: 12s

Agent: Re-running test...
$ ./build/libraries/SVO/tests/Debug/test_rebuild_hierarchy.exe \
    --gtest_filter=SVORebuildTest.BasicRebuild

PASSED
```

### Green Phase
```
Agent: Updating metadata...
// @last-pass 2024-12-03

Agent: Running regression check...
$ ./build/libraries/SVO/tests/Debug/test_rebuild_hierarchy.exe --gtest_brief=1
[==========] 8 tests ran. 8 PASSED.

Agent: Test complete. Moving to next in queue.
```

---

## Example 2: Obsolete Test Detection

**Scenario:** `test_compression::LegacyDXT` references removed code.

### Yellow Phase
```
Agent: Checking test relevance...

Test: test_compression::LegacyDXT
File: libraries/SVO/tests/test_compression.cpp:89

Searching for tested function: DXTCompressor::compressLegacy
Result: NOT FOUND in codebase

Checking git history:
$ git log --oneline --all -- "**/DXTCompressor*"
abc1234 refactor: Replace legacy DXT with DXT1Compressor

Agent: Test is OBSOLETE
Reason: Tests removed LegacyDXT implementation, replaced by DXT1Compressor

Recommendation: REMOVE test
Alternative: Update to test DXT1Compressor::compress instead

Marking for removal and skipping to next test.
```

---

## Example 3: Test with Dependencies

**Scenario:** `test_streaming::AsyncLoad` fails, but depends on `test_io_basic`.

### Yellow Phase
```
Agent: Checking test relevance...

Test: test_streaming::AsyncLoad
Dependencies found: @dependencies test_io_basic::FileRead

Checking dependency status...
$ ./build/libraries/Core/tests/Debug/test_io_basic.exe \
    --gtest_filter=IOBasicTest.FileRead

FAILED: Cannot open test file

Agent: Dependency test is failing.
Action: DEFER current test, process dependency first.

Adding to queue:
1. test_io_basic::FileRead (PROMOTED - blocking dependency)
2. test_streaming::AsyncLoad (DEFERRED)
```

---

## Example 4: Performance Regression Detection

**Scenario:** `test_raycast::Performance` passes but is slower than baseline.

### Green Phase (with warning)
```
Agent: Test passed. Checking performance baseline...

Current runtime: 850ms
Baseline: @runtime-baseline 450ms
Drift: +89% (exceeds 20% threshold)

WARNING: Performance regression detected

Options:
1. Investigate regression (recommended)
2. Update baseline (if intentional)
3. Mark as known issue and continue

Agent: Investigating...
$ git diff HEAD~5 -- libraries/SVO/src/SVOTraversal.cpp

Found: New safety checks added in commit def5678
Impact: Additional bounds checking per ray step

Recommendation:
- Safety checks are necessary
- Update baseline to 850ms
- Add comment: "Baseline increased due to bounds checking (def5678)"
```

---

## Example 5: Batch Processing Multiple Failures

**Scenario:** 5 tests failing after major refactor.

```
Agent: Discovered 5 failing tests. Creating prioritized queue...

Analyzing dependencies and groupings...

Queue (ordered):
1. test_svo_basic::NodeCreation (foundational, no deps)
2. test_svo_basic::ChildMask (depends on #1)
3. test_rebuild::FromLeaves (depends on #1, #2)
4. test_raycast::BasicHit (depends on #1)
5. test_raycast::Traversal (depends on #4)

Agent: Starting with test_svo_basic::NodeCreation...

[Processes each test through Yellow->Red->Green cycle]

Progress:
- [x] test_svo_basic::NodeCreation (GREEN) - 2 min
- [x] test_svo_basic::ChildMask (GREEN) - 1 min
- [ ] test_rebuild::FromLeaves (RED) - in progress
- [ ] test_raycast::BasicHit (pending)
- [ ] test_raycast::Traversal (pending)

Total time: 8 min, 2/5 complete
```

---

## Build Commands Reference

```bash
# Full test suite (avoid during cycle)
./build/libraries/SVO/tests/Debug/test_all.exe

# Single test file
./build/libraries/SVO/tests/Debug/test_rebuild_hierarchy.exe

# Specific test case
./build/libraries/SVO/tests/Debug/test_rebuild_hierarchy.exe \
    --gtest_filter=SVORebuildTest.BasicRebuild

# Pattern matching
./build/libraries/SVO/tests/Debug/test_rebuild_hierarchy.exe \
    --gtest_filter=*Rebuild*

# Brief output (recommended)
./build/libraries/SVO/tests/Debug/test_rebuild_hierarchy.exe --gtest_brief=1

# List tests without running
./build/libraries/SVO/tests/Debug/test_rebuild_hierarchy.exe --gtest_list_tests
```
