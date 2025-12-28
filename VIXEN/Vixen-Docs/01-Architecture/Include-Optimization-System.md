# Include Optimization System

**Status**: Planned
**HacknPlan**: Task creation specification in memory-bank/include-optimization-hacknplan-spec.md
**Last Updated**: 2025-12-28
**Task Reference**: [Build System] Optimize C++ include hierarchy for 30-45% build time reduction

## Overview

This design document outlines the architecture for optimizing C++ include dependencies across the VIXEN codebase to reduce build times by 30-45%.

## Problem Statement

The VIXEN codebase has 505 files in the `libraries/` directory with suboptimal include patterns causing unnecessarily long compilation times:

### Critical Issues Identified

| Issue | Count | Impact | Severity |
|-------|-------|--------|----------|
| Headers including `vulkan/vulkan.h` directly | 35 | Long recompilation chains | CRITICAL |
| Files depending on `VulkanDevice.h` | 78 | Tight coupling, broad impact | HIGH |
| Libraries missing PCH | 5 | Repeated compilation of common headers | HIGH |
| Files with redundant STL includes | 50+ | Unnecessary preprocessing | MEDIUM |
| Headers with unnecessary `<iostream>` | 15 | Debug-time includes | MEDIUM |
| Headers including `glm/glm.hpp` | 39 | Should be in PCH | MEDIUM |

### Current State Analysis

- **Total files**: 505
- **Precompiled header coverage**: 7 of 12 libraries have PCH
- **Missing PCH libraries**: Profiler, ShaderManagement, VulkanResources, Core, logger
- **Average include chain depth**: 8-12 levels

## Solution Architecture

### Phase 1: Forward Declarations (Refactor vulkan.h)

**Objective**: Eliminate direct vulkan.h includes from headers

**Approach**:
1. Create `libraries/Core/include/VulkanForwardDeclarations.h`
   - Forward declare all Vulkan types used across the codebase
   - Include complete vulkan.h only when needed

2. Update 35 headers:
   - Replace `#include <vulkan/vulkan.h>` with forward declarations
   - Move full include to corresponding `.cpp` files only

3. Benefits:
   - Headers change less frequently → fewer recompilations
   - .cpp file changes don't trigger header recompilation
   - Clearer dependency graph

### Phase 2: Precompiled Headers Consolidation

**Objective**: Add PCH to 5 missing libraries and standardize PCH across all libraries

**New PCH Files**:

| Library | File | Key Contents |
|---------|------|--------------|
| Profiler | `Profiler/src/pch.h` | STL, logging, timing |
| ShaderManagement | `ShaderManagement/src/pch.h` | STL, GLM, shader utilities |
| VulkanResources | `VulkanResources/src/pch.h` | Vulkan, GLM, resource types |
| Core | `Core/src/pch.h` | STL, memory utilities, types |
| logger | `logger/src/pch.h` | STL, logging macros |

**Standard PCH Contents**:
```cpp
// Standard Library (always included)
#include <vector>
#include <map>
#include <unordered_map>
#include <string>
#include <memory>
#include <array>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <cmath>

// Math Library
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// Project-specific common headers
#include <Core/Types.h>
#include <Core/Utilities.h>
```

### Phase 3: Include Cleanup

**Objective**: Remove redundant and unnecessary includes

**Tasks**:
1. Audit 50+ files with redundant STL includes
   - Remove includes that are already in pch.h
   - Use `/std:stdlatetest` compiler flag for maximum optimization

2. Clean up iostream usage (15 headers)
   - Remove from production headers
   - Move to pch.h for cpp files that need it
   - Use logging framework instead

3. Consolidate GLM includes (39 headers)
   - All `#include <glm/glm.hpp>` moved to PCH
   - Specific GLM headers in PCH (matrix_transform, type_ptr, etc.)
   - Remove redundant GLM includes from individual cpp files

### Phase 4: VulkanDevice.h Decoupling

**Objective**: Reduce the 78 files depending on VulkanDevice.h

**Strategy**:
1. Profile dependencies
   - Build dependency graph of VulkanDevice.h
   - Identify classes/functions that need full definition vs. just forward declaration

2. Create VulkanDeviceForwardDecl.h
   - Forward declare VulkanDevice
   - Declare access functions that don't need full definition

3. Refactor 78 files:
   - Replace includes with forward declarations where possible
   - Keep full includes only in `.cpp` files that truly need full VulkanDevice definition
   - Target: Reduce direct dependencies to < 15 files

## Implementation Sequence

### Week 1: Foundation
1. Create VulkanForwardDeclarations.h
2. Add PCH files to 5 missing libraries
3. Update CMakeLists.txt for PCH compilation flags

### Week 2: Migration
1. Replace vulkan.h includes in headers (35 files)
2. Update VulkanDevice.h dependents (78 files)
3. Clean redundant STL includes (50+ files)

### Week 3: Verification & Optimization
1. Remove iostream from headers (15 files)
2. Consolidate GLM includes (39 files)
3. Build time benchmarking and validation

## Build System Changes

### CMakeLists.txt Updates

All libraries using PCH must set:

```cmake
# Enable PCH for the target
target_precompile_headers(TargetName PRIVATE src/pch.h)

# Ensure dependencies have PCH
target_precompile_headers(TargetName REUSE_FROM LibraryDependency)
```

### Compiler Flags

```cmake
# C++ standards compliance
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:stdlatest /permissive-")

# Optimization
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Ox /GL")
```

## Expected Outcomes

### Performance Improvements
- **Incremental build time**: 30-45% reduction
- **Full rebuild time**: 20-30% reduction
- **Header file changes**: Reduced impact on dependent files

### Code Quality
- **Clearer dependency graph**: Easier to understand component coupling
- **Reduced include chains**: Max depth reduced from 8-12 to 4-6 levels
- **Better separation of concerns**: Headers focused on interface, not implementation

### Verification Metrics
- Build time before: Baseline measurements
- Build time after: Target 30-45% improvement
- Include chain depth: Max depth tracking
- Compilation unit size: Reduction in object file sizes

## Risks & Mitigation

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|-----------|
| Missing forward declarations | Medium | Compilation failures | Thorough testing in each phase |
| PCH cache invalidation | Low | Slow rebuilds | Monitor CMake cache invalidation |
| Circular dependencies | Medium | Linker errors | Automated dependency analysis |
| Platform compatibility | Low | Build failures on other platforms | Cross-platform testing |

## Dependencies

- C++23 compiler with PCH support
- CMake 3.18+ for `target_precompile_headers`
- No external library changes required

## Files Modified

### New Files (7)
- `libraries/Core/include/VulkanForwardDeclarations.h`
- `Profiler/src/pch.h`
- `ShaderManagement/src/pch.h`
- `VulkanResources/src/pch.h`
- `Core/src/pch.h`
- `logger/src/pch.h`

### Modified Files (500+)
- 35 headers (vulkan.h removal)
- 78 files (VulkanDevice.h reduction)
- 50+ files (STL include cleanup)
- 15 headers (iostream removal)
- 39 headers (GLM consolidation)
- ~12 CMakeLists.txt files

## Related Tasks

- HacknPlan: [#XXX] Include Optimization for Build Time Reduction
- Related Issues: Build performance optimization, compilation time reduction

## References

- [CMake: target_precompile_headers](https://cmake.org/cmake/help/latest/command/target_precompile_headers.html)
- [C++ Best Practices: Include Guard Patterns](https://www.cplusplus.com/articles/y8hbqMoL/)
- MSVC PCH Documentation: `/Yu` and `/Yc` flags
