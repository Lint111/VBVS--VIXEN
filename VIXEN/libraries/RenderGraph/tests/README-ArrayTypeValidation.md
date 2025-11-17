# Array Type Validation Test

## Overview

**Standalone compile-time test** for the array-aware type validation system.

This test validates that the `ResourceTypeTraits` system correctly handles:
- Scalar types (VkImage, VkBuffer, etc.)
- Vector types (std::vector<VkImage>)
- Array types (std::array<VkImage, N>)
- ResourceHandleVariant (macro-generated variant)
- Custom variants (std::variant<VkImage, VkBuffer, ...>)

## Key Feature: No Vulkan Runtime Required!

This test **only requires Vulkan HEADERS**, not the full Vulkan SDK or runtime libraries.

**Why?**
- All validation happens at **compile time** via `static_assert`
- No Vulkan API calls or GPU interaction
- Tests type traits and template metaprogramming only

## Building

### Option 1: With System Vulkan SDK

If Vulkan SDK is installed:
```bash
cd VIXEN
mkdir build && cd build
cmake ..
cmake --build . --target test_array_type_validation
```

### Option 2: Without Vulkan SDK (Automatic)

CMake will automatically fetch Vulkan headers from GitHub:
```bash
cd VIXEN
mkdir build && cd build
cmake ..  # Fetches Vulkan-Headers if needed
cmake --build . --target test_array_type_validation
```

**What happens**:
1. CMake checks for system Vulkan SDK (`find_package(Vulkan)`)
2. If not found, fetches `Vulkan-Headers` from KhronosGroup/Vulkan-Headers
3. Provides header-only Vulkan types (VkImage, VkBuffer, etc.)
4. Test builds successfully!

## Running

```bash
./test_array_type_validation
```

**Expected output**:
```
=== Array Type Validation Tests ===
Test 1: Scalar types
  VkImage valid: 1
  VkBuffer valid: 1

Test 2: Vector types
  vector<VkImage> valid: 1
  vector<VkBuffer> valid: 1

...

✅ All tests passed!
```

## What's Validated

### Compile-Time (30+ static_asserts)

1. **Scalar types**: `ResourceTypeTraits<VkImage>::isValid`
2. **Vector types**: `ResourceTypeTraits<vector<VkImage>>::isValid`
3. **Array types**: `ResourceTypeTraits<array<VkImage, 10>>::isValid`
4. **ResourceHandleVariant**: `ResourceTypeTraits<ResourceHandleVariant>::isValid`
5. **Custom variants**: `ResourceTypeTraits<variant<VkImage, VkBuffer>>::isValid`
6. **Invalid types**: `!ResourceTypeTraits<UnknownType>::isValid`
7. **Container detection**: `isVector`, `isArray`, `arraySize` metadata
8. **Slot definitions**: All test config slots compile successfully

If any static_assert fails, **compilation fails** = test caught error early!

### Runtime (Informational)

Prints validation results to confirm compile-time checks.

## Files

- **test_array_type_validation.cpp** - Test implementation
- **test_array_validation.cmake** - CMake configuration
- **../../cmake/VulkanHeaders.cmake** - Vulkan header fetching logic

## CI/CD Integration

### GitHub Actions

```yaml
- name: Build array type validation test
  run: |
    cd VIXEN && mkdir build && cd build
    cmake ..
    cmake --build . --target test_array_type_validation

- name: Run array type validation test
  run: |
    cd VIXEN/build
    ./test_array_type_validation
```

**No Vulkan SDK installation required!** Headers auto-fetch.

### Docker

```dockerfile
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    cmake \
    g++ \
    git

# No need to install Vulkan SDK!
# CMake will fetch headers automatically

WORKDIR /app
COPY . .
RUN cd VIXEN && mkdir build && cd build && cmake .. && make test_array_type_validation
```

## Troubleshooting

### Issue: "vulkan/vulkan.h not found"

**Solution**: Ensure CMake includes VulkanHeaders.cmake:

```cmake
include(${CMAKE_SOURCE_DIR}/cmake/VulkanHeaders.cmake)
```

### Issue: "C++23 not supported"

**Solution**: Update compiler:
- GCC ≥ 11
- Clang ≥ 15
- MSVC ≥ 2022

### Issue: FetchContent fails

**Solution**: Check internet connection or use local Vulkan headers:

```bash
export VULKAN_SDK=/path/to/vulkan/sdk
cmake -DVulkan_INCLUDE_DIR=$VULKAN_SDK/include ..
```

## Performance

**Compile time**: ~5-10 seconds (first build, includes header fetch)
**Runtime**: <100ms (just prints results)
**Binary size**: ~50KB (minimal executable)

## Future Enhancements

1. **Add benchmark mode**: Measure compile-time trait evaluation
2. **Test more edge cases**: Nested containers, cv-qualified types
3. **Integration with main test suite**: Add to CTest dashboard
4. **WASM support**: Compile to WebAssembly for browser testing

## Related Documentation

- [ArrayTypeValidation-Implementation.md](../../documentation/GraphArchitecture/ArrayTypeValidation-Implementation.md)
- [ResourceHandleVariant-Support.md](../../documentation/GraphArchitecture/ResourceHandleVariant-Support.md)
- [PolymorphicSlotSystem-Design.md](../../documentation/GraphArchitecture/PolymorphicSlotSystem-Design.md)
