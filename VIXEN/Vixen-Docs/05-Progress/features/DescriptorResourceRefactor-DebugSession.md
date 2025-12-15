# Descriptor Resource Refactor - Debug Session Summary

## Status: IN PROGRESS (BLOCKED)
**HacknPlan Task:** #61
**Date:** 2025-12-14
**Duration:** ~4 hours of debugging
**Related Tasks:** #79 (redundant hook execution), #80 (hook de-duplication)

---

## HANDOFF FOR NEXT AGENT

### Current State
Validation errors persist. The stored VkBuffer values are **correct** when traced through `BuildDescriptorWrites`, but validation reports **different** invalid handles.

### Key Evidence
```
[HandleBuffer] binding=4, VkBuffer=1820012101744 (0x1A7C0AB0C70)
vkUpdateDescriptorSets(): Invalid VkBuffer Object 0x1a7c12cc070  <-- DIFFERENT!
```

The addresses are close but not identical, suggesting:
1. Multiple descriptor sets being updated (trace one, error from another)
2. Wrapper object destruction timing issue
3. Memory corruption or stale validation cache

### What Was Tried
1. Fixed Execute-phase resource replacement (preserves original Resource with extractor)
2. Verified PostCompile hooks fire AFTER CompileImpl (correct timing)
3. Verified vector reservation prevents reallocation (not the cause)
4. Added extensive debug tracing to HandleBuffer, BuildDescriptorWrites, vkUpdateDescriptorSets

### Recommended Next Steps
1. **Add Vulkan debug names** to descriptor sets to identify which one fails
2. **Add destructor logging** to ShaderCountersBuffer and RayTraceBuffer
3. **Verify wrapper lifecycle** - are they destroyed between PostCompile and descriptor update?
4. **Check for multiple DescriptorSetNodes** - are there multiple being updated?

### Files With Debug Output (CLEANUP NEEDED)
- `CompileTimeResourceSystem.h` - SetHandle, GetDescriptorHandle
- `DescriptorResourceGathererNode.cpp` - StoreRegularResource
- `DescriptorSetNode.cpp` - HandleBuffer, BuildDescriptorWrites, vkUpdateDescriptorSets

---

## Issue Overview

Runtime validation errors in VIXEN benchmark: invalid VkBuffer handles at bindings 4 and 8 (wrapper types).

**Error Pattern:**
```
vkUpdateDescriptorSets(): pDescriptorWrites[3].pBufferInfo[0].buffer Invalid VkBuffer
vkUpdateDescriptorSets(): pDescriptorWrites[5].pBufferInfo[0].buffer Invalid VkBuffer
```

Note: `pDescriptorWrites[3]` = binding 4 (traceWriteIndex/RayTraceBuffer)
      `pDescriptorWrites[5]` = binding 8 (shaderCounters/ShaderCountersBuffer)

---

## Root Cause Chain (Chronological Discovery)

### 1. Initial Symptom
Wrapper types (`ShaderCountersBuffer*`, `RayTraceBuffer*`) returned VK_NULL_HANDLE when `GetDescriptorHandle()` was called.

### 2. First Red Herring: Debug Build Exception Swallowing
Debug builds return `T{}` (null) instead of throwing when type extraction fails.
**Fix:** Added null check before returning from GetHandle<VkBuffer>().

### 3. Second Red Herring: MSVC Concept Caching Bug
The `HasConversionType` concept evaluated to `false` even when `conversion_type` existed.
**Fix:** Changed to inline `requires` expression in `SetHandle`:
```cpp
if constexpr (requires { typename PointeeT::conversion_type; }) {
    // Create extractor
}
```

### 4. Third Red Herring: Execute-Phase Resource Replacement
`DescriptorResourceGathererNode::ExecuteImpl` was replacing resource pointers with fresh Resources that lacked extractors.
**Fix:** Preserve original Resource pointer, use its extractor for handle extraction.

### 5. Current Mystery: Handle Mismatch
Stored VkBuffer values are correct when printed, but validation reports different invalid handles.

---

## Investigation Findings

### PostCompile Hook Timing
- Hooks fire AFTER source node's `CompileImpl()` completes (correct)
- Hooks execute TWICE (once in NodeInstance::Compile, once in GeneratePipelines) - redundant but harmless
- Created backlog task #79 to remove redundancy

### Vector Reallocation
- Reserve is working correctly (capacity increases before push_back)
- No reallocation warnings triggered
- Not the cause of invalid handles

### Handle Storage
- `StoreRegularResource` stores correct VkBuffer values
- `BuildDescriptorWrites` retrieves correct VkBuffer values
- `HandleBuffer` stores correct pBufferInfo pointer
- Yet validation reports different invalid handles

---

## Files Modified

| File | Changes |
|------|---------|
| `CompileTimeResourceSystem.h` | Type traits fix, SetHandle/GetDescriptorHandle debug output |
| `DescriptorResourceGathererNode.cpp` | Execute-phase fix, StoreRegularResource debug output |
| `DescriptorSetNode.cpp` | HandleBuffer/BuildDescriptorWrites debug output |
| `VoxelGridNode.cpp` | static_assert for conversion_type |

---

## Artifacts Created

1. **Debugging skill:** `.claude/skills/debugging-known-issues/skill.md`
   - Known issues catalog
   - Debugging methodology
   - Prevention strategies

2. **Backlog tasks:**
   - #79: Remove redundant PostCompile hook execution (1h estimate)
   - #80: Add hook de-duplication (2h estimate)

---

## Why This Was Hard to Debug

1. **Multiple layers of indirection** - Resource → Wrapper → VkBuffer
2. **MSVC-specific bugs** - Concept caching required workaround
3. **Red herrings** - 3 partial fixes before finding current mystery
4. **Insufficient observability** - No built-in lifecycle tracing
5. **Handle mismatch** - Traced handles don't match validation errors

---

## Tags

#debugging #vulkan #descriptor-management #render-graph #wrapper-types #blocked
