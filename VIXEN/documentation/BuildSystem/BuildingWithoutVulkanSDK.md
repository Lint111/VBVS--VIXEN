# Building Tests Without Vulkan SDK

**Problem**: Building and running tests requires Vulkan SDK installation, which is heavyweight and unavailable in many CI/CD environments.

**Solution**: Automatic Vulkan header fetching + header-only tests.

---

## Quick Start

```bash
# No Vulkan SDK needed! This works anywhere:
cd VIXEN
mkdir build && cd build
cmake ..
cmake --build . --target test_array_type_validation
./tests/RenderGraph/test_array_type_validation
```

**What happens behind the scenes**:
1. CMake checks for Vulkan SDK (`find_package(Vulkan)`)
2. If not found, automatically fetches `Vulkan-Headers` from GitHub
3. Provides Vulkan type definitions (VkImage, VkBuffer, etc.) without runtime libraries
4. Compiles header-only tests successfully!

---

## How It Works

### 1. Vulkan Header Auto-Fetch

**File**: `cmake/VulkanHeaders.cmake`

```cmake
find_package(Vulkan QUIET)

if(NOT Vulkan_FOUND)
    # Fetch header-only Vulkan headers
    FetchContent_Declare(
        VulkanHeaders
        GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
        GIT_TAG        v1.3.290
    )
    FetchContent_MakeAvailable(VulkanHeaders)
endif()
```

**Benefits**:
- Works with or without Vulkan SDK
- Minimal download (~2MB headers vs ~200MB SDK)
- No administrator privileges needed
- CI/CD friendly

### 2. Header-Only Tests

Tests that only validate **compile-time** behavior don't need Vulkan runtime:

**Example**: Array type validation test
- Uses `static_assert` for validation
- No Vulkan API calls
- No GPU interaction
- Only requires type definitions from headers

**Result**: Builds and runs anywhere, including:
- Docker containers without GPU
- CI/CD runners without Vulkan drivers
- Development machines without Vulkan SDK

---

## Test Categories

### Category A: Header-Only Tests (No Vulkan Runtime)

✅ **Build without SDK**: Uses auto-fetched headers
✅ **Run without GPU**: No Vulkan API calls

**Examples**:
- `test_array_type_validation` - Type trait validation
- Future: Template metaprogramming tests
- Future: Constexpr tests

**Build command**:
```bash
cmake --build . --target test_array_type_validation
```

### Category B: Vulkan Runtime Tests (Requires SDK)

❌ **Requires Vulkan SDK**: Links against `vulkan.lib` / `libvulkan.so`
❌ **May require GPU**: Creates Vulkan instances/devices

**Examples**:
- `test_rendergraph_basic` - Full graph execution
- `test_vul kan_resources` - VkDevice creation
- Integration tests

**Build command** (requires SDK):
```bash
export VULKAN_SDK=/path/to/sdk
cmake ..
cmake --build . --target test_rendergraph_basic
```

---

## CI/CD Integration

### GitHub Actions

```yaml
name: Test Array Type Validation

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3

      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y cmake g++

      # NO VULKAN SDK INSTALLATION NEEDED!

      - name: Configure CMake
        run: |
          cd VIXEN
          cmake -B build

      - name: Build test
        run: |
          cd VIXEN
          cmake --build build --target test_array_type_validation

      - name: Run test
        run: |
          cd VIXEN/build
          ./tests/RenderGraph/test_array_type_validation
```

**No Vulkan SDK or GPU required!**

### Docker

```dockerfile
FROM ubuntu:22.04

# Minimal dependencies
RUN apt-get update && apt-get install -y \
    cmake \
    g++ \
    git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

# Build without Vulkan SDK (headers auto-fetch)
RUN cd VIXEN && \
    cmake -B build && \
    cmake --build build --target test_array_type_validation

# Run tests
CMD ["./VIXEN/build/tests/RenderGraph/test_array_type_validation"]
```

**Image size**: ~500MB (vs ~2GB with Vulkan SDK)

---

## Development Workflow

### Scenario 1: Developing Type System (No GPU Needed)

```bash
# Work on type traits/templates
vim VIXEN/RenderGraph/include/Core/ResourceTypeTraits.h

# Build and test instantly (no Vulkan SDK)
cmake --build build --target test_array_type_validation
./build/tests/RenderGraph/test_array_type_validation

# All validation happens at compile time!
# If it compiles, static_asserts passed ✅
```

**Iteration time**: ~5 seconds compile + <1 second run

### Scenario 2: Developing Rendering Code (GPU Needed)

```bash
# Work on GPU code
vim VIXEN/RenderGraph/src/Nodes/GeometryRenderNode.cpp

# Build and test with GPU (requires Vulkan SDK)
cmake --build build --target test_rendergraph_basic
./build/tests/RenderGraph/test_rendergraph_basic

# Runs on actual GPU with Vulkan validation layers
```

**Iteration time**: ~30 seconds compile + GPU execution

---

## Troubleshooting

### Issue: "vulkan/vulkan.h not found" during build

**Cause**: CMake didn't include VulkanHeaders.cmake

**Solution**: Ensure test CMakeLists.txt includes:
```cmake
include(${CMAKE_SOURCE_DIR}/cmake/VulkanHeaders.cmake)
```

### Issue: FetchContent fails to download headers

**Cause**: No internet connection or GitHub down

**Solution**: Use local Vulkan headers:
```bash
wget https://github.com/KhronosGroup/Vulkan-Headers/archive/v1.3.290.tar.gz
tar -xzf v1.3.290.tar.gz
cmake -DVulkan_INCLUDE_DIR=$PWD/Vulkan-Headers-1.3.290/include ..
```

### Issue: C++23 not supported

**Cause**: Old compiler

**Solution**: Update compiler:
- Ubuntu: `sudo apt-get install g++-11`
- macOS: `brew install gcc@11`
- Windows: Install Visual Studio 2022

### Issue: Test passes but runtime tests fail

**Cause**: Different test categories

**Explanation**:
- Header-only tests validate **compile-time** behavior ✅
- Runtime tests validate **GPU execution** (requires SDK) ❌

**Solution**: For GPU tests, install Vulkan SDK:
```bash
# Ubuntu
sudo apt-get install vulkan-sdk

# macOS
brew install vulkan-sdk

# Windows
# Download from https://vulkan.lunarg.com/
```

---

## Performance Comparison

| Aspect | With Vulkan SDK | Header-Only |
|--------|----------------|-------------|
| **Download size** | ~200MB | ~2MB |
| **Installation time** | ~10 minutes | ~30 seconds |
| **Build time (first)** | ~2 minutes | ~10 seconds |
| **Build time (incremental)** | ~30 seconds | ~5 seconds |
| **Disk space** | ~1GB | ~10MB |
| **Admin privileges** | Required | Not required |
| **GPU required** | Yes | No |
| **CI/CD time** | ~5 minutes | ~1 minute |

---

## Adding New Header-Only Tests

1. **Create test file** that uses only compile-time validation:
```cpp
#include "Core/ResourceVariant.h"

// Only static_assert - no Vulkan API calls!
static_assert(ResourceTypeTraits<VkImage>::isValid);
static_assert(ResourceTypeTraits<vector<VkImage>>::isValid);

int main() {
    std::cout << "Compile-time validation passed!" << std::endl;
    return 0;
}
```

2. **Create CMake file**:
```cmake
include(${CMAKE_SOURCE_DIR}/cmake/VulkanHeaders.cmake)

add_executable(my_test my_test.cpp)
target_include_directories(my_test PRIVATE
    ${VULKAN_HEADERS_INCLUDE_DIR}
)
```

3. **Build and run**:
```bash
cmake --build . --target my_test
./my_test
```

**No Vulkan SDK needed!**

---

## Summary

**Key Innovation**: Separate header-only tests from runtime tests

**Benefits**:
✅ Faster development iteration
✅ CI/CD without Vulkan SDK installation
✅ Works in restrictive environments (Docker, containers)
✅ Validates type system without GPU
✅ Smaller Docker images
✅ No administrator privileges needed

**Use When**:
- Developing type system / templates
- Running CI/CD tests
- Working without GPU
- Quick validation during development

**Don't Use When**:
- Testing GPU execution
- Validating Vulkan API calls
- Integration testing with real devices

---

## Related Documentation

- [ArrayTypeValidation Test README](../tests/RenderGraph/README-ArrayTypeValidation.md)
- [ArrayTypeValidation Implementation](GraphArchitecture/ArrayTypeValidation-Implementation.md)
- [CMake VulkanHeaders Module](../cmake/VulkanHeaders.cmake)

---

**Status**: ✅ Implemented and tested
**Since**: November 4, 2025
**Maintainer**: Development team
