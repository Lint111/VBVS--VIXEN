# Descriptor Resource Refactor - Debug Session Summary

## Status: RESOLVED ✅
**HacknPlan Task:** #61
**Date:** 2025-12-14 to 2025-12-15
**Duration:** ~10 hours over 2 days
**Related Tasks:** #77, #78 (cleanup crashes), #79, #80 (hook de-duplication)

---

## RESOLUTION SUMMARY

### Root Cause
`HasConversionType_v<T>` type trait failed to detect `conversion_type` typedef on `ShaderCountersBuffer` and `RayTraceBuffer` wrapper classes.

**Why:** In `VoxelGridNodeConfig.h:6-10`, both classes were **forward-declared** instead of fully included:
```cpp
// BROKEN - forward declaration
namespace Vixen::RenderGraph::Debug {
    class ShaderCountersBuffer;  // Only declares, doesn't define
    class RayTraceBuffer;
}
```

When `Resource::SetHandle<Debug::ShaderCountersBuffer*>()` was called, the SFINAE check `std::void_t<typename T::conversion_type>` couldn't see the `conversion_type` typedef because the class was incomplete. The primary template (returning `false`) was selected **silently** - no compile error.

Result: `descriptorExtractor_` lambda was never captured, and `GetDescriptorHandle()` returned stale cached handles instead of extracting fresh VkBuffer values.

### Fix Applied
Changed `VoxelGridNodeConfig.h` to include full headers:
```cpp
// FIXED - full includes for wrapper types with conversion_type
#include "Debug/ShaderCountersBuffer.h"
#include "Debug/RayTraceBuffer.h"
```

### Files Modified
| File | Change |
|------|--------|
| `VoxelGridNodeConfig.h:5-13` | Full includes instead of forward-declarations |
| `CompileTimeResourceSystem.h:934-939` | Removed debug cout |
| `VoxelGridNode.cpp:242-246` | Updated comment documenting fix |
| `troubleshooting.md` | Added SFINAE detection checklist |

### Verification
- Build succeeded
- Benchmark runs without "Invalid VkBuffer" validation errors
- Resource tracker shows `ExtractorCalled` events confirming extractors work

---

## Why This Bug Was Hard to Debug (~10 hours)

### 1. Symptom vs Cause Mismatch
**Symptom:** "Invalid VkBuffer Object 0xd500000000d5" in vkUpdateDescriptorSets
**Assumed Cause:** Resource lifetime/cleanup timing issue
**Actual Cause:** C++ template metaprogramming SFINAE failure due to header inclusion order

The garbage-looking handle values (0xd500000000d5) suggested memory corruption or use-after-free, leading investigation down wrong paths for most of Day 1.

### 2. SFINAE Fails Silently
When `std::void_t<typename T::conversion_type>` fails because T is forward-declared, the primary template (returning `false`) is selected **silently**:
- No compile error
- No runtime error
- No warning
- Just wrong behavior

### 3. Multiple Layers of Indirection
Handle extraction path:
```
ctx.Out() → Resource::SetHandle() → HasConversionType_v → descriptorExtractor_ capture
         → GetDescriptorHandle() → DescriptorResourceEntry::GetHandle() → vkUpdateDescriptorSets
```
Bug was in layer 3 (HasConversionType_v), symptoms appeared in layer 6 (vkUpdateDescriptorSets).

### 4. Debug Infrastructure Created Mid-Investigation
Had to create `DescriptorResourceTracker.h` (400+ lines) to trace handle values through the entire pipeline. This was necessary but added ~2 hours of debug infrastructure work.

### 5. Template Instantiation Point Not Obvious
The template `SetHandle<T>()` is instantiated where called (VoxelGridNode.cpp), but its behavior depends on what headers are visible at that point, which depends on include chains through VoxelGridNodeConfig.h.

### 6. Red Herrings
Before finding root cause, several partial issues were identified and fixed:
- Debug build exception swallowing (GetHandle returns T{} not throw)
- MSVC concept caching (switched to inline requires)
- Execute-phase resource replacement (preserving original Resource)
- Cleanup order issues (Resource::Clear() before wrapper destruction)

Each seemed like "the fix" until testing revealed the error persisted.

---

## Prevention for Future

### Pattern: Wrapper Types with conversion_type
Any class with `using conversion_type = VkBuffer/VkImageView/etc` that's used in node config slots **MUST be fully included** (not forward-declared) in the config header.

### Troubleshooting Checklist (Added to rules/troubleshooting.md)
When seeing "Invalid VkHandle" errors with wrapper types:
1. Check if wrapper declares `conversion_type`
2. Check if wrapper is forward-declared vs fully included where used
3. Add `static_assert(HasConversionType_v<WrapperType>)` to verify detection
4. Check for `ExtractorCreated` events in DescriptorResourceTracker output

---

## Debug Infrastructure Created

### DescriptorResourceTracker.h
Location: `libraries/RenderGraph/include/Debug/DescriptorResourceTracker.h`

Comprehensive tracking system for descriptor resources:
```cpp
// Enable via VIXEN_DEBUG_DESCRIPTOR_TRACKING (auto in Debug)
TRACK_RESOURCE_CREATED(id, binding, handle, type, node);
TRACK_EXTRACTOR_CREATED(id, binding, node);
TRACK_EXTRACTOR_CALLED(id, binding, handle, type, node);
TRACK_HANDLE_EXTRACTED(id, binding, handle, type, node, info);
TRACK_HANDLE_BOUND(id, binding, handle, type, node);

DUMP_RESOURCE_TRACKING();  // Dump all events
DUMP_BINDING_TRACKING(8);  // Dump events for binding 8
```

This infrastructure will be valuable for future descriptor-related debugging.

---

## Investigation Timeline

### Day 1 (2025-12-14) - 4 hours
- Initial validation errors observed
- Fixed Execute-phase resource replacement
- Investigated PostCompile hook timing
- Created #79 (redundant hooks) and #80 (hook de-duplication)
- Created debugging skill
- **Blocked:** Handles still invalid

### Day 2 (2025-12-15) - 6 hours
- Created DescriptorResourceTracker.h
- Traced handle values through entire pipeline
- Discovered handle mismatch between stored and invalid values
- Root cause identified: HasConversionType_v returning false
- Traced to forward-declaration in VoxelGridNodeConfig.h
- Fix applied and verified
- Documentation updated

---

## Related Issues Fixed During Investigation

| Task | Issue | Fix |
|------|-------|-----|
| #77 | QueryPool cleanup crash | Release before graph destruction |
| #78 | Shared state cleanup crash | Guard against null device |

---

## Tags

#debugging #vulkan #descriptor-management #render-graph #wrapper-types #sfinae #templates #resolved
