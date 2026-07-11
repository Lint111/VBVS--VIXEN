# Test Metadata Schema

Standard metadata annotations for C++ Google Test files in this project.

## Required Metadata (Yellow Phase)

```cpp
/**
 * @test TestSuiteName.TestName
 * @coverage List of functions/classes this test covers
 * @category unit | integration | performance | regression
 */
```

## Optional Metadata

```cpp
/**
 * @last-pass YYYY-MM-DD (auto-updated when test goes green)
 * @compile-time-baseline Xs (baseline compilation time)
 * @runtime-baseline Xms (baseline execution time)
 * @dependencies Comma-separated list of tests that must pass first
 * @owner Subsystem name (SVO, RenderGraph, Core, etc.)
 * @added YYYY-MM-DD (when test was created)
 * @modified YYYY-MM-DD (last structural change, not auto-updates)
 * @issue Link to issue/ticket if test was added for specific bug
 * @flaky true (if test has intermittent failures)
 * @skip-reason Reason if test is temporarily disabled
 */
```

## Category Definitions

| Category | Description | Typical Runtime |
|----------|-------------|-----------------|
| **unit** | Tests single function/class in isolation | < 100ms |
| **integration** | Tests multiple components together | 100ms - 1s |
| **performance** | Benchmarks, timing-sensitive | 1s - 30s |
| **regression** | Specific bug reproduction | varies |

## Coverage Notation

```cpp
// Single function
// @coverage SVORebuild::rebuildFromLeaves

// Multiple functions
// @coverage SVORebuild::rebuildFromLeaves, SVONode::computeChildMask

// Entire class
// @coverage SVORebuild::*

// Specific scenario
// @coverage SVORebuild::rebuildFromLeaves[depth>8]
```

## Example Annotated Test

```cpp
/**
 * @test SVORebuildTest.RebuildFromLeaves_Depth8
 * @coverage SVORebuild::rebuildFromLeaves, SVONode::computeChildMask
 * @category unit
 * @last-pass 2024-12-03
 * @compile-time-baseline 2.3s
 * @runtime-baseline 45ms
 * @dependencies test_svo_basic::NodeCreation
 * @owner SVO
 * @added 2024-11-15
 */
TEST_F(SVORebuildTest, RebuildFromLeaves_Depth8) {
    // Test implementation
}
```

## Metadata Update Rules

| Event | Update |
|-------|--------|
| Test passes | `@last-pass` = today |
| Test modified | `@modified` = today |
| Baseline drift > 20% | Update baseline, investigate |
| Test removed | Document in removal commit |

## File-Level Metadata

At top of test file:

```cpp
/**
 * @file test_rebuild_hierarchy.cpp
 * @brief Tests for SVO hierarchy rebuild operations
 * @subsystem SVO
 * @total-tests 12
 * @last-full-pass 2024-12-01
 */
```
