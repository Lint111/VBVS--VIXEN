# CashSystem Documentation

**Status**: Phase 2 Complete - Integrated with RenderGraph

## Overview

Type-safe resource caching system with virtual cleanup architecture. Eliminates redundant Vulkan resource creation via hash-based deduplication.

## Core Documentation

1. **[01-architecture.md](01-architecture.md)** - System architecture, patterns, cleanup flow
2. **[02-usage-guide.md](02-usage-guide.md)** - API guide, common patterns, integration

## Tracking Documents

- **[CashSystem-Migration-Status.md](CashSystem-Migration-Status.md)** - Migration progress tracking (transient)

## Active Cachers

1. **ShaderModuleCacher** - VkShaderModule caching with CACHE HIT/MISS logging
2. **PipelineCacher** - VkPipeline caching with activity tracking
3. **PipelineLayoutCacher** - VkPipelineLayout sharing (transparent two-mode API)

## Key Features

- ✅ Virtual `Cleanup()` method for polymorphic destruction
- ✅ TypedCacher template with hash-based keys
- ✅ MainCacher registry for global orchestration
- ✅ Device-dependent cache cleanup via DeviceNode
- ✅ Zero validation errors, proper resource destruction

## Related

- See `memory-bank/systemPatterns.md` - CashSystem patterns
- See `RenderGraph/src/Nodes/GraphicsPipelineNode.cpp` - Cacher usage example
