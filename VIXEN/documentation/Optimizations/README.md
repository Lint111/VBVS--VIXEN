# VIXEN Stack Optimization System

**Phase H Initiative**: Memory optimization through stack allocation

---

## Overview

This directory contains documentation and design for VIXEN's **stack optimization system**, which moves predetermined resource allocations from heap to stack for improved performance and responsiveness.

**Key Benefits**:
- ⚡ **Zero per-frame heap allocations** in hot paths
- 📊 **10-20 μs faster frame times** (estimated)
- 🎯 **Better cache locality** through contiguous stack memory
- 🛡️ **Safety guaranteed** with debug tracking and warnings
- 🔧 **Easy migration** via StackAllocatedRM wrapper

---

## Documents

### 1. [StackOptimization-PhaseH.md](./StackOptimization-PhaseH.md)
**Comprehensive optimization plan**

Detailed analysis of:
- 30+ heap allocation sites identified for optimization
- Priority ranking (high/medium/low impact)
- Performance impact estimates
- Migration strategy
- Testing and validation plan

**Key Sections**:
- Hot path optimizations (SwapChainNode, DescriptorSetNode, etc.)
- Pipeline configuration optimizations
- Stack safety analysis
- Performance profiling methodology

---

### 2. [StackRM-Integration.md](./StackRM-Integration.md)
**Integration with Resource Management**

How stack optimization integrates with VIXEN's RM system:
- StackAllocatedRM<T, N> design
- Type aliases for common Vulkan resources
- Migration patterns and examples
- Diagnostics and monitoring
- Best practices

**Key Components**:
- `StackAllocatedRM<T, N>` - Unified stack/RM wrapper
- Pre-defined aliases (StackImageViewArray, etc.)
- Automatic StackTracker integration
- RM state management for stack resources

---

## Quick Start

### For Developers

**Using stack-allocated resources**:
```cpp
#include "ResourceManagement/StackAllocatedRM.h"

void MyNode::Execute() {
    // Replace: std::vector<VkImageView> views;
    StackImageViewArray views("MyNode:views");

    views.Add(colorView);
    views.Add(depthView);

    // Use with Vulkan APIs
    fbInfo.attachmentCount = views.Size();
    fbInfo.pAttachments = views.Data();
}
```

**Common type aliases**:
- `StackImageViewArray` - Image views (swapchain, attachments)
- `StackDescriptorWriteArray` - Descriptor updates
- `StackShaderStageArray` - Pipeline shader stages
- `StackCommandBufferArray` - Command buffers
- See [StackRM-Integration.md](./StackRM-Integration.md) for full list

---

### For Performance Analysis

**Enable stack tracking (debug builds)**:
```cpp
void RenderGraph::Execute() {
    STACK_TRACKER_RESET_FRAME();

    // Execute graph...

    // Print stats every 100 frames
    if (frameCount % 100 == 0) {
        STACK_TRACKER_PRINT_STATS();
    }
}
```

**Monitor individual resources**:
```cpp
StackDescriptorWriteArray writes("descriptor:writes");
// ... use writes ...

writes.PrintStats();  // Per-resource diagnostics
```

---

## Architecture

### Components

```
┌─────────────────────────────────────────────────────────────┐
│                    VIXEN Stack Optimization                  │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│  ┌────────────────────────────────────────────────────┐     │
│  │            VulkanLimits.h                          │     │
│  │  • MAX_* constants for compile-time bounds          │     │
│  │  • Conservative Vulkan spec limits                  │     │
│  │  • Stack safety thresholds                          │     │
│  └────────────────────────────────────────────────────┘     │
│                           │                                   │
│                           ▼                                   │
│  ┌────────────────────────────────────────────────────┐     │
│  │         StackAllocatedRM<T, N>                     │     │
│  │  • std::array<T, N> storage (stack)                │     │
│  │  • RM state management integration                 │     │
│  │  • Vector-like API (Add, Size, Data)               │     │
│  │  • Automatic StackTracker integration              │     │
│  └────────────────────────────────────────────────────┘     │
│                           │                                   │
│                           ▼                                   │
│  ┌────────────────────────────────────────────────────┐     │
│  │         StackTracker (Debug Only)                  │     │
│  │  • Thread-local cumulative tracking                │     │
│  │  • Warning/critical thresholds                     │     │
│  │  • Per-frame statistics                            │     │
│  │  • Zero overhead in release builds                 │     │
│  └────────────────────────────────────────────────────┘     │
│                                                               │
└─────────────────────────────────────────────────────────────┘
```

---

## Files Created

### Headers
- `/VIXEN/RenderGraph/include/Core/VulkanLimits.h`
  - Compile-time constants for resource limits
  - Stack safety thresholds

- `/VIXEN/RenderGraph/include/Core/StackTracker.h`
  - Debug tracking utility (header-only)
  - RAII scope tracking
  - Statistics reporting

- `/VIXEN/ResourceManagement/include/ResourceManagement/StackAllocatedRM.h`
  - Stack-allocated RM wrapper
  - Type aliases for Vulkan resources
  - Migration helpers

### Implementation
- `/VIXEN/RenderGraph/src/Core/StackTracker.cpp`
  - Statistics and reporting implementation
  - Warning threshold handlers

---

## Migration Roadmap

### Phase 1: Infrastructure ✅ COMPLETE
- [x] VulkanLimits.h created
- [x] StackTracker.h/.cpp created
- [x] StackAllocatedRM.h created
- [x] Documentation complete

### Phase 2: Hot Path Optimizations (Week 2) 🎯 NEXT
- [ ] SwapChainNode::GetColorImageViews()
- [ ] DescriptorSetNode temporary vectors
- [ ] FramebufferNode attachments
- [ ] WindowNode event processing

### Phase 3: Pipeline Configuration (Week 3)
- [ ] GraphicsPipelineNode shader stages
- [ ] DescriptorSetNode layout bindings
- [ ] Vertex input descriptions
- [ ] Command buffer handle conversions

### Phase 4: Long-Lived Objects (Week 4)
- [ ] DeviceNode member arrays
- [ ] PerFrameResources frame array
- [ ] Cache structure optimizations

### Phase 5: Validation & Profiling (Week 5)
- [ ] Full test suite with StackTracker
- [ ] Performance profiling
- [ ] Allocation count verification
- [ ] Documentation of results

---

## Performance Targets

| Metric | Before | Target | Measurement |
|--------|--------|--------|-------------|
| Heap allocations/frame | 30-50 | <5 | Valgrind massif |
| Frame time improvement | Baseline | +10-20 μs | GPU timestamps |
| Stack usage (peak) | N/A | <50 KB | StackTracker |
| Stack usage (warning) | N/A | Never | StackTracker |
| Cache miss rate | Baseline | -5-10% | Hardware counters |

---

## Safety Guarantees

### Compile-Time Safety
- All capacity limits defined as constants
- Template instantiation validates sizes
- No runtime size negotiation

### Debug-Time Safety
- Bounds checking on all operations
- StackTracker warns at 512 KB threshold
- Critical error at 1 MB threshold
- Per-frame overflow detection

### Runtime Safety (Release)
- Conservative capacity estimates (90x headroom)
- Typical usage: ~11 KB per frame
- Stack size: 1-8 MB (platform dependent)
- Safety factor: ~100x

---

## Testing

### Unit Tests
- Basic StackAllocatedRM operations
- RM state integration
- Bounds checking (debug)
- Overflow detection

### Integration Tests
- Full render graph execution
- Resize event handling
- StackTracker validation
- Multi-threaded access

### Stress Tests
- Maximum capacity usage
- Rapid allocations
- Event bursts
- Long-running sessions

### Performance Tests
- Allocation count profiling
- Frame time measurements
- Cache performance analysis
- Stack usage validation

---

## Resources

### Documentation
- [StackOptimization-PhaseH.md](./StackOptimization-PhaseH.md) - Complete optimization plan
- [StackRM-Integration.md](./StackRM-Integration.md) - Integration guide
- [VIXEN/memory-bank/systemPatterns.md](../../memory-bank/systemPatterns.md) - Architecture patterns

### Code
- [VulkanLimits.h](../../RenderGraph/include/Core/VulkanLimits.h) - Constants
- [StackTracker.h](../../RenderGraph/include/Core/StackTracker.h) - Debug tracking
- [StackAllocatedRM.h](../../ResourceManagement/include/ResourceManagement/StackAllocatedRM.h) - Wrapper

### External References
- [Vulkan Specification Limits](https://registry.khronos.org/vulkan/specs/1.3/html/vkspec.html#limits)
- [C++ Stack vs Heap Performance](https://stackoverflow.com/questions/79923/what-and-where-are-the-stack-and-heap)
- [Small Vector Optimization](https://akrzemi1.wordpress.com/2020/01/23/small-vector-optimization/)

---

## FAQ

**Q: Why not use std::vector everywhere?**
A: Heap allocation has overhead (~100-500ns per allocation). For fixed-size resources allocated every frame (30-50 times), this adds up to measurable frame time impact.

**Q: Is stack overflow a concern?**
A: No. Our conservative limits use ~11 KB per frame. Typical stack sizes are 1-8 MB, giving us ~100x safety margin. StackTracker provides warnings well before danger.

**Q: What about multi-threading?**
A: StackTracker uses thread_local storage. Each thread has independent tracking. StackAllocatedRM is not thread-safe for concurrent access (same as std::vector).

**Q: Can I use this for large arrays?**
A: No. StackAllocatedRM is for small, bounded arrays (<10 KB). For large/variable-size data, continue using std::vector.

**Q: What's the overhead of StackAllocatedRM?**
A: Debug builds: StackTracker overhead (~50-100ns per scope). Release builds: Zero overhead (all tracking macros become no-ops).

**Q: How do I know if my constants are too large?**
A: StackTracker will warn you. If you hit warnings, reduce MAX_* constants in VulkanLimits.h. Document any changes.

---

## Contact

For questions or suggestions about stack optimization:

1. Check existing documentation first
2. Review examples in [StackRM-Integration.md](./StackRM-Integration.md)
3. Examine StackTracker output in debug builds
4. File issues in project tracker

---

**Last Updated**: 2025-11-11
**Phase**: H (Voxel Infrastructure)
**Status**: Infrastructure Complete, Ready for Migration
