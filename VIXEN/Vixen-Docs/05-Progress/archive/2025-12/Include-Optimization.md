---
title: Include Optimization for Build Time Reduction
status: IN_PROGRESS
priority: Critical
created: 2025-12-28
tags: [build-system, optimization, performance]
---

# Include Optimization for Build Time Reduction

## Overview
Systematic optimization of C++ include dependencies across 505 files in libraries/ to reduce build times by 30-45%.

## Analysis Summary

### Critical Issues (20%+ build impact)
| Issue | Files Affected | Impact |
|-------|---------------|--------|
| vulkan/vulkan.h in headers | 35 headers | CRITICAL |
| VulkanDevice.h overuse | 78 files | HIGH |
| Missing PCH files | 5 libraries | HIGH |

### High Priority Issues (10-15% impact)
| Issue | Files Affected | Impact |
|-------|---------------|--------|
| Redundant STL includes after pch.h | ~50 files | MEDIUM |
| iostream in headers | 15 headers | MEDIUM |
| GLM not in unified PCH | 39 headers | MEDIUM |

## Implementation Plan

### Phase 1: VulkanForwardDeclarations.h
Create centralized Vulkan type forward declarations to eliminate vulkan.h parsing in headers.

### Phase 2: Add PCH to Missing Libraries
- Core
- logger
- Profiler
- ShaderManagement
- VulkanResources

### Phase 3: VulkanDevice.h Forward Declarations
Replace full includes with forward declarations where only pointer/reference used.

### Phase 4: Redundant Include Cleanup
Remove STL includes from .cpp files that are already in pch.h.

### Phase 5: iostream Removal
Remove iostream from non-pch headers, move to .cpp files.

### Phase 6: GLM Consolidation
Add glm/glm.hpp to unified PCH for all libraries using GLM.

## Progress Log
- 2025-12-28: Analysis complete, implementation starting

## Files Changed
(To be updated during implementation)

## Cross-References
- HacknPlan: Task specification in memory-bank/include-optimization-hacknplan-spec.md
- Design Element: Pending creation (type: System=9)
- Architecture: Vixen-Docs/01-Architecture/Include-Optimization-System.md
