# VS Code Testing Framework Setup

**Last Updated:** 2025-11-05

This document describes the VS Code testing framework integration for the VIXEN RenderGraph project.

---

## Table of Contents

1. [Overview](#overview)
2. [Required Extensions](#required-extensions)
3. [Configuration Files](#configuration-files)
4. [Test Organization](#test-organization)
5. [Running Tests](#running-tests)
6. [Code Coverage](#code-coverage)
7. [Debugging Tests](#debugging-tests)
8. [Troubleshooting](#troubleshooting)

---

## Overview

The VIXEN project uses VS Code's native testing framework with the following features:

- **Automatic test discovery** from CMake/CTest
- **Test Explorer UI** for hierarchical test organization
- **Code coverage visualization** with inline gutters
- **Integrated debugging** for individual tests
- **Folder structure representation** matching source organization

### Test Categories

Tests are organized by functionality:

| Category | Pattern | Description |
|----------|---------|-------------|
| Type System | `test_array_*` | Array and type validation |
| Field Extraction | `test_field_*` | Struct field extraction |
| Resource Tests | `test_resource_*` | Resource management and gatherer |
| Graph Tests | `test_graph_*` | Graph topology and dependencies |
| Descriptor Tests | `test_descriptor_*` | Descriptor resource gathering |
| Core Tests | `test_rendergraph_*` | Basic RenderGraph functionality |
| TypedNode Tests | `test_typednode_*` | TypedNode helpers |

---

## Required Extensions

Install these VS Code extensions for full testing support:

### Essential

```json
"ms-vscode.cmake-tools"           // CMake integration
"ms-vscode.cpptools"              // C++ IntelliSense
"hbenl.vscode-test-explorer"     // Test Explorer UI
"ryanluker.vscode-coverage-gutters" // Coverage visualization
```

### Recommended

```json
"matepek.vscode-catch2-test-adapter" // C++ test adapter
"DavidSchuldenfrei.gtest-adapter"    // GoogleTest adapter
"twxs.cmake"                          // CMake syntax
```

Install all recommended extensions:
```bash
# Open command palette (Ctrl+Shift+P)
# Type: Extensions: Show Recommended Extensions
# Click "Install All"
```

---

## Configuration Files

### `.vscode/settings.json`

Main configuration for test discovery and coverage:

```json
{
  "testMate.cpp.test.executables": "{build}/**/*{test,Test,TEST}*",
  "cmake.testExplorer.enabled": true,
  "coverage-gutters.coverageBaseDir": "${workspaceFolder}/VIXEN/build"
}
```

**Key Settings:**
- Test executable discovery patterns
- CMake integration
- Coverage file locations
- Test categories for filtering

### `.vscode/tasks.json`

Build and test automation tasks:

**Available Tasks:**
- `cmake: configure` - Configure CMake
- `cmake: build` - Build all targets
- `cmake: build RenderGraph tests` - Build only test targets
- `test: run all RenderGraph tests` - Run all tests via CTest
- `coverage: generate report` - Generate LCOV coverage report
- `coverage: open report` - Open HTML coverage report

**Run tasks via:**
- Command Palette: `Tasks: Run Task`
- Keyboard: `Ctrl+Shift+B` (default build task)

### `.vscode/launch.json`

Debug configurations for individual tests:

**Available Configurations:**
- `C++ Test (Debug)` - Debug current CMake target
- `RenderGraph: Array Type Validation`
- `RenderGraph: Field Extraction`
- `RenderGraph: Resource Gatherer`
- `RenderGraph: Resource Management`
- `RenderGraph: Graph Topology`
- `Test with Coverage` - Run with coverage instrumentation

**Debug a test:**
1. Set breakpoints in test file
2. Press `F5` or select configuration
3. Test will pause at breakpoints

---

## Test Organization

### Folder Structure

```
VIXEN/
├── tests/
│   └── RenderGraph/
│       ├── test_array_type_validation.cpp     ✅ Type System
│       ├── test_field_extraction.cpp          ✅ Field Extraction
│       ├── test_resource_gatherer.cpp         ✅ Resource Gatherer
│       ├── test_resource_management.cpp       ✅ Resource Management (NEW)
│       ├── test_graph_topology.cpp            ✅ Graph Topology (NEW)
│       ├── test_descriptor_gatherer_comprehensive.cpp
│       ├── test_rendergraph_basic.cpp
│       ├── test_rendergraph_dependency.cpp
│       └── test_typednode_helpers.cpp
└── build/
    └── tests/
        └── RenderGraph/
            ├── test_array_type_validation     (executable)
            ├── test_field_extraction           (executable)
            └── ... (mirrors source structure)
```

### Test Explorer Hierarchy

Tests appear in VS Code's Test Explorer organized by:

```
VIXEN (workspace)
└── tests
    └── RenderGraph
        ├── Type System Tests
        │   └── test_array_type_validation
        ├── Field Extraction Tests
        │   └── test_field_extraction
        ├── Resource Tests
        │   ├── test_resource_gatherer
        │   └── test_resource_management
        └── Graph Tests
            └── test_graph_topology
```

---

## Running Tests

### Method 1: Test Explorer UI

1. Open Test Explorer (sidebar icon or `Ctrl+Shift+T`)
2. Click refresh icon to discover tests
3. Click play button next to test name
4. View results inline

**Features:**
- Run individual tests or suites
- Re-run failed tests only
- Filter by test name
- Show/hide passing tests

### Method 2: Command Palette

```
Ctrl+Shift+P
> Test: Run All Tests
> Test: Run Test at Cursor
> Test: Debug Test at Cursor
```

### Method 3: CMake Tasks

Run via tasks.json:

```bash
# Build tests
Ctrl+Shift+P > Tasks: Run Task > cmake: build RenderGraph tests

# Run all tests
Ctrl+Shift+P > Tasks: Run Task > test: run all RenderGraph tests

# Run specific test
Ctrl+Shift+P > Tasks: Run Task > test: array type validation
```

### Method 4: Terminal (CTest)

```bash
cd VIXEN/build

# Run all tests
ctest --verbose

# Run RenderGraph tests only
ctest -R RenderGraph --verbose

# Run specific test
ctest -R test_array_type_validation --verbose

# Run with output on failure
ctest --output-on-failure
```

### Method 5: Direct Execution

```bash
cd VIXEN/build

# Run test executable directly
./tests/RenderGraph/test_array_type_validation

# With GoogleTest filters
./tests/RenderGraph/test_resource_management --gtest_filter="*Budget*"

# With verbose output
./tests/RenderGraph/test_graph_topology --gtest_verbose
```

---

## Code Coverage

### Setup Coverage Build

1. **Configure with coverage:**
   ```bash
   cd VIXEN
   cmake -B build -DENABLE_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug
   ```

2. **Build with coverage:**
   ```bash
   cmake --build build --target coverage
   ```

   This automatically:
   - Builds tests with coverage instrumentation
   - Runs all tests
   - Generates `coverage.info` (LCOV format)
   - Creates HTML report in `build/coverage_html/`

### View Coverage in VS Code

**Coverage Gutters Extension:**

1. Run tests with coverage (see above)
2. Open any source file
3. Coverage shows inline:
   - **Green highlight** - Line covered
   - **Orange highlight** - Partially covered
   - **Red highlight** - Not covered
   - **No highlight** - Not executable

**Toggle coverage display:**
- Command Palette: `Coverage Gutters: Display Coverage`
- Keyboard: `Ctrl+Shift+7`

**Watch mode:**
- Command Palette: `Coverage Gutters: Watch`
- Auto-updates on test run

### View HTML Coverage Report

```bash
# Generate and open in browser
cd VIXEN/build
cmake --build . --target coverage

# Or use VS Code task
Ctrl+Shift+P > Tasks: Run Task > coverage: open report
```

**HTML Report Features:**
- **Per-file coverage percentages**
- **Line-by-line execution counts**
- **Function coverage**
- **Branch coverage**
- **Navigable file tree**

### Coverage Metrics

**Interpreting Results:**

| Metric | Meaning |
|--------|---------|
| Line Coverage | % of lines executed |
| Function Coverage | % of functions called |
| Branch Coverage | % of branches taken |

**Target Coverage:**
- **Excellent:** 80-100%
- **Good:** 60-79%
- **Partial:** 40-59%
- **Poor:** 0-39%

**Current RenderGraph Coverage:** ~40%

---

## Debugging Tests

### Debug Individual Test

1. **Set breakpoints** in test file
2. **Select debug configuration:**
   - Press `F5`
   - Or: Run & Debug sidebar → Select configuration
3. **Step through code:**
   - `F10` - Step over
   - `F11` - Step into
   - `Shift+F11` - Step out
   - `F5` - Continue

### Debug with GoogleTest Filters

Modify `launch.json` args:

```json
{
  "name": "Debug Specific Test",
  "args": [
    "--gtest_filter=ResourceBudgetManagerTest.SetBudget",
    "--gtest_break_on_failure"
  ]
}
```

### Debug Test Failures

**On test failure:**

1. Test Explorer shows ❌ icon
2. Click test name for failure message
3. Set breakpoint near failure
4. Debug test (F5)
5. Inspect variables in Debug Console

**Common breakpoint locations:**
- `EXPECT_*` assertions
- `ASSERT_*` assertions
- Test setup/teardown
- Before/after operation

### Debug with GDB

```bash
cd VIXEN/build

# Run test under GDB
gdb ./tests/RenderGraph/test_resource_management

# GDB commands
(gdb) break test_resource_management.cpp:45
(gdb) run
(gdb) print budgetManager
(gdb) backtrace
```

---

## Troubleshooting

### Tests Not Discovered

**Problem:** Test Explorer shows no tests

**Solutions:**

1. **Rebuild compile_commands.json:**
   ```bash
   cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
   ```

2. **Refresh CMake cache:**
   ```
   Ctrl+Shift+P > CMake: Delete Cache and Reconfigure
   ```

3. **Check test executables exist:**
   ```bash
   ls -la VIXEN/build/tests/RenderGraph/test_*
   ```

4. **Verify settings.json pattern:**
   ```json
   "testMate.cpp.test.executables": "{build}/**/*{test,Test,TEST}*"
   ```

### Coverage Not Showing

**Problem:** Coverage gutters show no data

**Solutions:**

1. **Verify coverage.info exists:**
   ```bash
   ls -la VIXEN/build/coverage.info
   ```

2. **Check coverage file path:**
   ```json
   "coverage-gutters.coverageBaseDir": "${workspaceFolder}/VIXEN/build"
   ```

3. **Regenerate coverage:**
   ```
   Ctrl+Shift+P > Tasks: Run Task > coverage: generate report
   ```

4. **Display coverage:**
   ```
   Ctrl+Shift+P > Coverage Gutters: Display Coverage
   ```

### Build Failures

**Problem:** Tests fail to build

**Solutions:**

1. **Clean rebuild:**
   ```
   Ctrl+Shift+P > Tasks: Run Task > clean: build directory
   Ctrl+Shift+P > Tasks: Run Task > cmake: configure
   Ctrl+Shift+P > Tasks: Run Task > cmake: build
   ```

2. **Check trimmed build mode:**
   ```bash
   # Tests may require full Vulkan SDK
   cmake -B build -DVULKAN_TRIMMED_BUILD=OFF
   ```

3. **Verify dependencies:**
   ```bash
   # Check GoogleTest available
   cmake -B build --debug-output | grep googletest
   ```

### Debug Hangs

**Problem:** Debug session hangs on launch

**Solutions:**

1. **Check executable exists:**
   ```bash
   ls -la VIXEN/build/tests/RenderGraph/test_*
   ```

2. **Verify GDB path:**
   ```json
   "miDebuggerPath": "/usr/bin/gdb"
   ```

3. **Use verbose logging:**
   ```json
   "logging": {
     "engineLogging": true
   }
   ```

---

## Quick Reference

### Keyboard Shortcuts

| Action | Shortcut |
|--------|----------|
| Run all tests | `Ctrl+Shift+T` → Run All |
| Debug test at cursor | `Ctrl+Shift+D` |
| Toggle coverage | `Ctrl+Shift+7` |
| Build (default task) | `Ctrl+Shift+B` |
| Open command palette | `Ctrl+Shift+P` |

### Common Commands

```bash
# Configure
cmake -B build -DENABLE_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug

# Build tests
cmake --build build --target <test_name>

# Run all
ctest --verbose

# Coverage
cmake --build build --target coverage

# Clean
rm -rf build
```

### File Locations

| Item | Path |
|------|------|
| Test source | `VIXEN/tests/RenderGraph/test_*.cpp` |
| Test executables | `VIXEN/build/tests/RenderGraph/test_*` |
| Coverage data | `VIXEN/build/coverage.info` |
| HTML report | `VIXEN/build/coverage_html/index.html` |
| Compile commands | `VIXEN/build/compile_commands.json` |

---

## Additional Resources

- [VS Code C++ Testing](https://code.visualstudio.com/docs/cpp/cpp-debug)
- [CMake Tools Extension](https://github.com/microsoft/vscode-cmake-tools)
- [Coverage Gutters](https://marketplace.visualstudio.com/items?itemName=ryanluker.vscode-coverage-gutters)
- [GoogleTest Documentation](https://google.github.io/googletest/)
- [LCOV Documentation](http://ltp.sourceforge.net/coverage/lcov.php)

---

**For issues or improvements, see:**
- `VIXEN/RenderGraph/docs/TEST_COVERAGE.md` - Detailed coverage analysis
- `VIXEN/RenderGraph/docs/TEST_COVERAGE_SUMMARY.md` - Quick reference
