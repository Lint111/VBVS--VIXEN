# VIXEN Test Suite

This directory contains the unit and integration tests for the VIXEN project using Google Test.

## Overview

The test suite is organized by library/module:

```
tests/
├── ShaderManagement/       # Tests for ShaderManagement library
│   ├── test_shader_cache_manager.cpp
│   ├── test_shader_preprocessor.cpp
│   ├── test_shader_compiler.cpp
│   └── test_integration.cpp
├── (Future: EventBus/)
├── (Future: ResourceManagement/)
└── (Future: RenderGraph/)
```

## Building Tests

Tests are built automatically when `BUILD_TESTS` is enabled (default: ON).

### CMake Configuration

```bash
cd build
cmake .. -DBUILD_TESTS=ON
cmake --build .
```

### Disable Tests

```bash
cmake .. -DBUILD_TESTS=OFF
```

## Running Tests

### Run All Tests

```bash
# From build directory
ctest

# Or with more output
ctest --output-on-failure

# Or with verbose output
ctest --verbose
```

### Run Specific Test Suite

```bash
# Run only ShaderManagement tests
ctest -L ShaderManagement

# Run tests matching a pattern
ctest -R "ShaderCache"
```

### Run Tests with Make

```bash
# If using make
make test

# Or run specific test executable directly
./ShaderManagement_Tests
```

### Advanced CTest Options

```bash
# Run tests in parallel
ctest -j4

# Show only failed tests
ctest --rerun-failed --output-on-failure

# Run with specific configuration
ctest -C Debug
ctest -C Release

# List all tests without running
ctest -N
```

## Test Organization

### ShaderManagement Tests

#### test_shader_cache_manager.cpp
- **Purpose**: Tests for shader cache persistence and management
- **Coverage**:
  - Cache storage and retrieval
  - Cache validation and eviction
  - Thread safety
  - Statistics tracking
  - Cache key generation

**Example test cases**:
- `StoreAndLookup` - Basic cache operations
- `ConcurrentStoreAndLookup` - Thread safety
- `StatisticsHitMiss` - Cache hit/miss tracking
- `ValidateCache` - Cache integrity validation

#### test_shader_preprocessor.cpp
- **Purpose**: Tests for GLSL preprocessing
- **Coverage**:
  - Define injection
  - Include resolution
  - Circular include prevention
  - Global and local defines
  - Include path management

**Example test cases**:
- `InjectSimpleDefine` - Preprocessor define handling
- `SimpleInclude` - Include file resolution
- `CircularIncludePrevention` - Include guard tests
- `NestedIncludes` - Complex include hierarchies

#### test_shader_compiler.cpp
- **Purpose**: Tests for GLSL to SPIR-V compilation
- **Coverage**:
  - Shader compilation for all stages
  - Compilation options (optimization, debug info)
  - Error handling and reporting
  - SPIR-V validation and disassembly
  - Custom entry points

**Example test cases**:
- `CompileVertexShader` - Basic compilation
- `CompileWithOptimization` - Optimization options
- `CompileInvalidShader` - Error handling
- `ValidateSpirvValid` - SPIR-V validation

#### test_integration.cpp
- **Purpose**: Integration tests for full shader pipeline
- **Coverage**:
  - Preprocess → Compile → Cache workflow
  - Shader variant generation
  - Complex include hierarchies
  - Multi-stage programs
  - Real-world shader examples

**Example test cases**:
- `PreprocessCompileCache` - Full pipeline test
- `VariantGeneration` - Shader permutations
- `ComplexIncludeHierarchy` - Include system stress test
- `RealWorldPBRShader` - Realistic shader example

## Writing New Tests

### Adding Tests to Existing Suite

1. Add test file to appropriate directory (e.g., `tests/ShaderManagement/`)
2. Update `CMakeLists.txt` to include new file
3. Follow existing test patterns

### Creating New Test Suite

1. Create directory: `tests/YourLibrary/`
2. Create `CMakeLists.txt`:

```cmake
add_executable(YourLibrary_Tests
    test_feature1.cpp
    test_feature2.cpp
)

target_link_libraries(YourLibrary_Tests
    PRIVATE
        YourLibrary
        GTest::gtest
        GTest::gtest_main
)

set_property(TARGET YourLibrary_Tests PROPERTY CXX_STANDARD 23)

gtest_discover_tests(YourLibrary_Tests
    PROPERTIES
        LABELS "YourLibrary"
        TIMEOUT 30
)
```

3. Add to `tests/CMakeLists.txt`:

```cmake
add_subdirectory(YourLibrary)
```

### Test Naming Conventions

- Test files: `test_<feature>.cpp`
- Test fixtures: `<Feature>Test`
- Test cases: `<FeatureName>_<TestDescription>`

**Example**:
```cpp
class ShaderCacheManagerTest : public ::testing::Test { };

TEST_F(ShaderCacheManagerTest, StoreAndLookup) {
    // Test implementation
}
```

## Test Best Practices

### 1. Use Fixtures for Setup/Teardown

```cpp
class MyTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup code
    }

    void TearDown() override {
        // Cleanup code
    }
};
```

### 2. Use ASSERT vs EXPECT

- **ASSERT_**: Fatal assertion (stops test on failure)
- **EXPECT_**: Non-fatal assertion (continues test on failure)

```cpp
ASSERT_TRUE(result.success);  // Stop if false
EXPECT_EQ(value, 42);         // Continue if not equal
```

### 3. Test Isolation

- Each test should be independent
- Don't rely on test execution order
- Clean up resources in TearDown()

### 4. Descriptive Test Names

```cpp
// Good
TEST(ShaderCompilerTest, CompileWithOptimizationProducesValidSpirv)

// Bad
TEST(ShaderCompilerTest, Test1)
```

### 5. Use Temporary Directories

```cpp
void SetUp() override {
    testDir = std::filesystem::temp_directory_path() / "my_test";
    std::filesystem::create_directories(testDir);
}

void TearDown() override {
    std::filesystem::remove_all(testDir);
}
```

## Continuous Integration

Tests are automatically run on:
- Pre-commit hooks (optional)
- Pull request builds
- Main branch commits

### CI Configuration

Tests must:
- Pass with no failures
- Complete within timeout (30s per test by default)
- Not leak resources
- Be deterministic (no flaky tests)

## Code Coverage

To generate code coverage reports (Linux/Mac):

```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON
cmake --build .
ctest
lcov --capture --directory . --output-file coverage.info
genhtml coverage.info --output-directory coverage_html
```

View coverage:
```bash
open coverage_html/index.html
```

## Debugging Tests

### Run Single Test

```bash
./ShaderManagement_Tests --gtest_filter="ShaderCacheManagerTest.StoreAndLookup"
```

### Run with Debugger

```bash
gdb ./ShaderManagement_Tests
(gdb) run --gtest_filter="*"
```

### Verbose Output

```bash
./ShaderManagement_Tests --gtest_filter="*" --gtest_verbose
```

### Repeat Tests

```bash
# Run test 100 times to catch flaky tests
./ShaderManagement_Tests --gtest_repeat=100 --gtest_break_on_failure
```

## Performance Testing

Some tests include performance benchmarks:

```cpp
TEST_F(IntegrationTest, CachePerformanceComparison) {
    auto start = std::chrono::high_resolution_clock::now();
    // ... test code ...
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    EXPECT_LT(duration.count(), 100);  // Should complete in < 100ms
}
```

## Test Maintenance

### Regular Tasks

- [ ] Run tests before committing
- [ ] Update tests when adding features
- [ ] Remove obsolete tests
- [ ] Keep test coverage above 80%
- [ ] Fix flaky tests immediately

### When Tests Fail

1. Check if recent code changes broke tests
2. Check if test assumptions are still valid
3. Update test expectations if behavior changed intentionally
4. Fix code if behavior regressed
5. Never disable failing tests without fixing root cause

## Resources

- [Google Test Documentation](https://google.github.io/googletest/)
- [CMake CTest Documentation](https://cmake.org/cmake/help/latest/manual/ctest.1.html)
- [Testing Best Practices](https://google.github.io/googletest/primer.html)

## Getting Help

- Check existing tests for examples
- Review Google Test documentation
- Ask in project discussions
- Create an issue if tests are unclear
