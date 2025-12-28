# Include Optimization Task - READY FOR CREATION

**Status**: Complete specification prepared
**Date**: 2025-12-28
**Task Title**: [Build System] Optimize C++ include hierarchy for 30-45% build time reduction

---

## Summary

Complete HacknPlan task specification has been prepared for the Include Optimization feature. All documentation, architecture designs, and implementation plans are complete and cross-referenced.

## Key Information

| Field | Value |
|-------|-------|
| **Project** | VIXEN (230809) |
| **Board** | Sprint 2 - Data Collection Polish (649722) |
| **Category** | Programming |
| **Priority** | Urgent (Critical) |
| **Estimated Hours** | 40 |
| **Tags** | vulkan (1), refactor (7), performance (8) |
| **Assigned To** | Lior Yaari (230909) |

## Quick Creation Steps

### 1. Create Work Item
Use MCP call or HacknPlan UI with specification from:
- Location: `memory-bank/hacknplan-creation-instructions.md`
- Full spec: `memory-bank/include-optimization-hacknplan-spec.md`

### 2. Save Task ID
HacknPlan will return a task ID (e.g., #1234). Save this.

### 3. Create Design Element
- Type: System (9)
- Title: Include Optimization Architecture System
- Link to task ID from step 2

### 4. Update Vault Docs
Add task ID and design element ID to:
- `Vixen-Docs/05-Progress/features/Include-Optimization.md`
- `Vixen-Docs/01-Architecture/Include-Optimization-System.md`

## Implementation Phases (40 hours total)

1. **Phase 1: VulkanForwardDeclarations.h** (8h)
   - Create: libraries/Core/include/VulkanForwardDeclarations.h
   - Update: 35 headers to use forward declarations

2. **Phase 2: Add PCH to 5 Libraries** (8h)
   - Core, logger, Profiler, ShaderManagement, VulkanResources
   - Standardize PCH contents

3. **Phase 3: VulkanDevice.h Decoupling** (10h)
   - Refactor: 78 files dependent on VulkanDevice.h
   - Replace includes with forward declarations where possible

4. **Phase 4: Remove Redundant STL Includes** (6h)
   - Clean: ~50 files with redundant STL includes
   - Remove duplicates already in pch.h

5. **Phase 5: Remove iostream from Headers** (4h)
   - Clean: 15 headers with unnecessary iostream
   - Move to pch.h for cpp files that need it

6. **Phase 6: Consolidate GLM into PCH** (4h)
   - Add: glm/glm.hpp to unified PCH
   - Update: 39 headers to use PCH instead

## Prepared Documents

| Document | Purpose | Path |
|----------|---------|------|
| Task Specification | Complete MCP payload and details | memory-bank/include-optimization-hacknplan-spec.md |
| Task Summary | Quick reference and status | memory-bank/include-optimization-task-summary.md |
| Creation Instructions | Step-by-step creation guide | memory-bank/hacknplan-creation-instructions.md |
| Progress Tracking | Feature progress vault doc | Vixen-Docs/05-Progress/features/Include-Optimization.md |
| Architecture Design | System architecture document | Vixen-Docs/01-Architecture/Include-Optimization-System.md |

## Expected Outcomes

- **Build Time Improvement**: 30-45% reduction (incremental builds)
- **Full Rebuild**: 20-30% reduction
- **Include Depth**: Reduced from 8-12 levels to 4-6 levels
- **Code Quality**: Clearer component dependency graph

## Acceptance Criteria

- [ ] Build time improvement validated (30-45% reduction)
- [ ] All unit tests pass
- [ ] No new compiler warnings or errors
- [ ] Include chain depth verified and documented
- [ ] Code review approval

## Next Action

1. Execute HacknPlan task creation using payload from `include-optimization-hacknplan-spec.md`
2. Record the returned task ID
3. Create linked design element
4. Update vault documentation with task ID
5. Start Phase 1 implementation

---

All specification work is complete. Ready to proceed with HacknPlan task creation.
