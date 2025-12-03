# Phase Separation Migration Plan

## Goal
Refactor RenderGraph to have clear phase separation with proper data access boundaries:
- **Setup Phase**: Node initialization only (no input data access)
- **Compile Phase**: Data validation and Vulkan resource creation (has input data)
- **Deferred Connections**: Optimistic connection creation, validated during Compile

---

## Current Problems

1. **Setup and Compile are too similar** - both access input data, unclear separation
2. **Variadic slots must exist before connections** - requires PreRegisterVariadicSlots workaround
3. **GraphCompileSetup tries to read inputs** - connections not available yet
4. **No clear validation point** - validation scattered across Setup/Compile
5. **Hard to do incremental graph updates** - phases not well-defined

---

## Target Architecture

### Phase Flow
```
1. Graph Construction (User Code)
   ↓
2. Deferred Connection Processing (Create tentative slots)
   ↓
3. Topology Analysis (Build execution order)
   ↓
4. Setup Phase (Node initialization, NO input data)
   ↓
5. Compile Phase (Validate tentative slots, create resources, HAS input data)
   ↓
6. Execute Phase (Runtime per-frame execution)
```

### Phase Responsibilities

| Phase | Input Data Access | Resource Creation | Vulkan API | Purpose |
|-------|------------------|-------------------|------------|---------|
| **Construction** | ❌ | ❌ | ❌ | Build graph topology |
| **Deferred Connections** | ❌ | ❌ | ❌ | Create tentative slots |
| **Topology** | ❌ | ❌ | ❌ | Compute execution order |
| **Setup** | ❌ | ❌ | ✅ (query only) | Node self-init, get device/caches |
| **Compile** | ✅ | ✅ | ✅ | Validate slots, create VkPipeline/etc |
| **Execute** | ✅ | ❌ | ✅ | Record commands, submit |

---

## Migration Steps

### Step 1: Add Slot State Tracking
**Goal**: Track validation state of variadic slots

#### 1.1 Add SlotState Enum
**File**: `RenderGraph/include/Core/VariadicTypedNode.h`

```cpp
enum class SlotState {
    Tentative,    // Created during connection, unvalidated
    Validated,    // Type-checked during Compile
    Compiled,     // Finalized with resources
    Invalid       // Validation failed
};
```

#### 1.2 Update VariadicSlotInfo
**File**: `RenderGraph/include/Core/VariadicTypedNode.h`

```cpp
struct VariadicSlotInfo {
    Resource* resource = nullptr;
    ResourceType resourceType;
    std::string slotName;
    uint32_t binding = 0;
    VkDescriptorType descriptorType = VK_DESCRIPTOR_TYPE_MAX_ENUM;

    // NEW: Track validation state
    SlotState state = SlotState::Tentative;

    // NEW: Track source for error messages
    NodeHandle sourceNode;
    uint32_t sourceOutput = 0;
};
```

**Affected Files**:
- `RenderGraph/include/Core/VariadicTypedNode.h` - struct definition
- `RenderGraph/src/Nodes/DescriptorResourceGathererNode.cpp` - update slot creation

**Estimated Impact**: Small (struct change, backward compatible)

---

### Step 2: Modify ConnectVariadic to Be Optimistic
**Goal**: ConnectVariadic creates tentative slots without validation

#### 2.1 Update RenderGraph::ConnectVariadic
**File**: `RenderGraph/src/Core/RenderGraph.cpp`

```cpp
void RenderGraph::ConnectVariadic(
    NodeHandle targetHandle,
    size_t slotIndex,
    NodeHandle sourceHandle,
    uint32_t sourceOutputIdx,
    size_t bundleIndex
) {
    auto* targetNode = GetInstanceInternal(targetHandle);
    auto* sourceNode = GetInstanceInternal(sourceHandle);

    if (!targetNode || !sourceNode) {
        throw std::runtime_error("Invalid node handle in ConnectVariadic");
    }

    // Get output resource (create if needed)
    Resource* resource = sourceNode->GetOutput(sourceOutputIdx, 0);
    if (!resource) {
        resource = CreateResourceForOutput(sourceNode, sourceOutputIdx);
        sourceNode->SetOutput(sourceOutputIdx, 0, resource);
    }

    // Create tentative variadic slot (NO VALIDATION)
    // Validation happens during Compile phase
    VariadicSlotInfo tentativeSlot;
    tentativeSlot.resource = resource;
    tentativeSlot.resourceType = resource->GetType();  // Best guess
    tentativeSlot.slotName = "variadic_" + std::to_string(slotIndex);
    tentativeSlot.state = SlotState::Tentative;  // Mark as unvalidated
    tentativeSlot.sourceNode = sourceHandle;
    tentativeSlot.sourceOutput = sourceOutputIdx;
    tentativeSlot.binding = static_cast<uint32_t>(slotIndex);  // Default

    // Add to target node (always succeeds)
    auto* variadicNode = dynamic_cast<VariadicTypedNode<>*>(targetNode);
    if (!variadicNode) {
        throw std::runtime_error("Target node does not support variadic inputs");
    }

    // Ensure slot exists (resize if needed)
    while (variadicNode->GetVariadicInputCount(bundleIndex) <= slotIndex) {
        VariadicSlotInfo emptySlot;
        emptySlot.state = SlotState::Tentative;
        variadicNode->RegisterVariadicSlot(emptySlot, bundleIndex);
    }

    // Update slot with connection info
    variadicNode->UpdateVariadicSlot(slotIndex, tentativeSlot, bundleIndex);

    // Add dependency
    targetNode->AddDependency(sourceNode);

    std::cout << "[ConnectVariadic] Created tentative slot " << slotIndex
              << " on " << targetNode->GetInstanceName()
              << " from " << sourceNode->GetInstanceName() << std::endl;
}
```

#### 2.2 Add UpdateVariadicSlot Helper
**File**: `RenderGraph/include/Core/VariadicTypedNode.h`

```cpp
/**
 * @brief Update an existing variadic slot (used during connection)
 */
bool UpdateVariadicSlot(size_t index, const VariadicSlotInfo& slotInfo, size_t bundleIndex = 0) {
    if (bundleIndex >= variadicBundles_.size()) {
        return false;
    }

    auto& slots = variadicBundles_[bundleIndex].variadicSlots;
    if (index >= slots.size()) {
        return false;
    }

    slots[index] = slotInfo;
    return true;
}
```

**Affected Files**:
- `RenderGraph/include/Core/RenderGraph.h` - ConnectVariadic signature unchanged
- `RenderGraph/src/Core/RenderGraph.cpp` - implementation change
- `RenderGraph/include/Core/VariadicTypedNode.h` - new UpdateVariadicSlot method

**Estimated Impact**: Medium (core graph connection logic)

---

### Step 3: Separate Setup Responsibilities
**Goal**: Setup does node initialization only, no input data access

#### 3.1 Audit All Node SetupImpl Methods
**Files to Check**:
- `RenderGraph/src/Nodes/*.cpp` - all node implementations

**For each SetupImpl**:
- ✅ **Keep**: Device access, cache allocation, service registration
- ❌ **Move to Compile**: Input data reading, resource creation, validation

#### 3.2 Example: DescriptorResourceGathererNode
**File**: `RenderGraph/src/Nodes/DescriptorResourceGathererNode.cpp`

**BEFORE (Current)**:
```cpp
void DescriptorResourceGathererNode::SetupImpl(Context& ctx) {
    // ❌ Reading input data - should be in Compile!
    auto shaderBundle = ctx.In(SHADER_DATA_BUNDLE);
    DiscoverDescriptors(ctx);
}
```

**AFTER (Migrated)**:
```cpp
void DescriptorResourceGathererNode::SetupImpl(Context& ctx) {
    // ✅ Node initialization only
    std::cout << "[DescriptorResourceGathererNode::Setup] Node initialized\n";

    // Check if slots were pre-registered via PreRegisterVariadicSlots
    if (!descriptorSlots_.empty()) {
        std::cout << "[Setup] Using pre-registered slots ("
                  << descriptorSlots_.size() << " bindings)\n";
    } else {
        std::cout << "[Setup] Will discover slots during Compile phase\n";
    }

    // No input data access here!
}
```

#### 3.3 Move Discovery to Compile
**File**: `RenderGraph/src/Nodes/DescriptorResourceGathererNode.cpp`

```cpp
void DescriptorResourceGathererNode::CompileImpl(Context& ctx) {
    std::cout << "[DescriptorResourceGathererNode::Compile] Starting...\n";

    // Now we can access input data
    auto shaderBundle = ctx.In(SHADER_DATA_BUNDLE);
    if (!shaderBundle) {
        throw std::runtime_error("No shader bundle connected");
    }

    // Validate tentative slots against shader metadata
    ValidateTentativeSlotsAgainstShader(ctx, shaderBundle);

    // Validate variadic inputs (base validation)
    if (!ValidateVariadicInputsImpl(ctx)) {
        throw std::runtime_error("Variadic input validation failed");
    }

    // Gather resources
    GatherResources(ctx);

    // Output
    ctx.Out(DESCRIPTOR_RESOURCES, resourceArray_);
    ctx.Out(SHADER_DATA_BUNDLE_OUT, shaderBundle);
}
```

#### 3.4 Add ValidateTentativeSlotsAgainstShader
**File**: `RenderGraph/src/Nodes/DescriptorResourceGathererNode.cpp`

```cpp
void DescriptorResourceGathererNode::ValidateTentativeSlotsAgainstShader(
    Context& ctx,
    ShaderDataBundle* shaderBundle
) {
    if (!shaderBundle || !shaderBundle->descriptorLayout) {
        throw std::runtime_error("Invalid shader bundle");
    }

    const auto* layoutSpec = shaderBundle->descriptorLayout.get();
    size_t variadicCount = GetVariadicInputCount();

    std::cout << "[ValidateTentativeSlots] Shader has " << layoutSpec->bindings.size()
              << " bindings, graph has " << variadicCount << " variadic inputs\n";

    // Check count matches
    if (variadicCount != layoutSpec->bindings.size()) {
        throw std::runtime_error(
            "Variadic input count mismatch: shader expects " +
            std::to_string(layoutSpec->bindings.size()) + ", got " +
            std::to_string(variadicCount)
        );
    }

    // Validate each tentative slot
    for (size_t i = 0; i < variadicCount; ++i) {
        const auto* slotInfo = GetVariadicSlotInfo(i, 0);
        if (!slotInfo) continue;

        // Find corresponding shader binding
        const auto& shaderBinding = layoutSpec->bindings[i];

        // Update slot with shader metadata
        VariadicSlotInfo updatedSlot = *slotInfo;
        updatedSlot.binding = shaderBinding.binding;
        updatedSlot.descriptorType = shaderBinding.descriptorType;

        // Generate proper slot name
        switch (shaderBinding.descriptorType) {
            case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                updatedSlot.slotName = "storage_image_" + std::to_string(shaderBinding.binding);
                break;
            // ... other cases
        }

        // Validate resource type compatibility
        if (!ValidateResourceType(slotInfo->resource, shaderBinding.descriptorType)) {
            throw std::runtime_error(
                "Slot " + std::to_string(i) + " type mismatch: " +
                "shader expects descriptor type " + std::to_string(shaderBinding.descriptorType)
            );
        }

        // Mark as validated
        updatedSlot.state = SlotState::Validated;

        // Update slot
        UpdateVariadicSlot(i, updatedSlot, 0);

        std::cout << "[ValidateTentativeSlots] Slot " << i << " validated: "
                  << updatedSlot.slotName << " (binding=" << updatedSlot.binding << ")\n";
    }
}
```

**Affected Files**:
- `RenderGraph/src/Nodes/DescriptorResourceGathererNode.cpp` - SetupImpl, CompileImpl changes
- `RenderGraph/include/Nodes/DescriptorResourceGathererNode.h` - new method declaration

**Estimated Impact**: Medium (node behavior change, but backward compatible)

---

### Step 4: Remove GraphCompileSetup Usage
**Goal**: GraphCompileSetup is now optional (for pre-registration only)

#### 4.1 Remove GraphCompileSetup Override
**File**: `RenderGraph/include/Nodes/DescriptorResourceGathererNode.h`

```cpp
// REMOVE this override (revert to default no-op)
// void GraphCompileSetup() override;
```

**File**: `RenderGraph/src/Nodes/DescriptorResourceGathererNode.cpp`

```cpp
// REMOVE entire GraphCompileSetup implementation
// (lines 28-109)
```

#### 4.2 Update Documentation
**File**: `RenderGraph/include/Core/VariadicTypedNode.h`

Update GraphCompileSetup comment:
```cpp
/**
 * @brief IGraphCompilable implementation - no-op by default
 *
 * This is called during graph compilation BEFORE connections are processed.
 * Only use compile-time metadata here (e.g., Names.h constants).
 *
 * DO NOT access input data - connections are not available yet!
 *
 * Optional use: Pre-register expected slot count/structure
 * Example:
 *   void GraphCompileSetup() override {
 *       // Set expected count from compile-time metadata
 *       SetVariadicInputConstraints(ComputeTest::descriptorCount, ...);
 *   }
 */
void GraphCompileSetup() override {
    // Default: no-op
}
```

**Affected Files**:
- `RenderGraph/include/Nodes/DescriptorResourceGathererNode.h` - remove override
- `RenderGraph/src/Nodes/DescriptorResourceGathererNode.cpp` - remove implementation
- `RenderGraph/include/Core/VariadicTypedNode.h` - update documentation

**Estimated Impact**: Small (removes broken code)

---

### Step 5: Update VulkanGraphApplication Usage
**Goal**: Use new optimistic connection flow

#### 5.1 Remove PreRegisterVariadicSlots Call
**File**: `source/VulkanGraphApplication.cpp`

```cpp
// REMOVE these lines (lines 444-452):
/*
auto* gathererNode = dynamic_cast<DescriptorResourceGathererNode*>(
    renderGraph->GetInstance(descriptorGatherer));
if (gathererNode) {
    gathererNode->PreRegisterVariadicSlots(ComputeTest::outputImage);
    mainLogger->Info("Pre-registered variadic slots for compute descriptor gatherer");
}
*/
```

#### 5.2 Update ConnectVariadic Call (if needed)
**File**: `source/VulkanGraphApplication.cpp`

```cpp
// Connection now creates tentative slot automatically
renderGraph->ConnectVariadic(
    descriptorGatherer,
    0,  // slotIndex
    computeOutputImage,  // source node
    0   // source output
);
mainLogger->Info("Connected compute output to descriptor gatherer (tentative)");
```

**Affected Files**:
- `source/VulkanGraphApplication.cpp` - remove workaround, simplify usage

**Estimated Impact**: Small (simplifies user code)

---

### Step 6: Update VariadicTypedNode Validation
**Goal**: Validation checks slot state

#### 6.1 Update ValidateVariadicInputsImpl
**File**: `RenderGraph/include/Core/VariadicTypedNode.h`

```cpp
virtual bool ValidateVariadicInputsImpl(Context& ctx, size_t bundleIndex = 0) {
    size_t count = GetVariadicInputCount(bundleIndex);

    // Validate count constraints
    if (count < minVariadicInputs_ || count > maxVariadicInputs_) {
        std::cout << "[ValidateVariadicInputsImpl] ERROR: Count constraint violated\n";
        return false;
    }

    if (bundleIndex >= variadicBundles_.size()) {
        return count == 0;
    }

    const auto& variadicSlots = variadicBundles_[bundleIndex].variadicSlots;
    for (size_t i = 0; i < variadicSlots.size(); ++i) {
        const auto& slotInfo = variadicSlots[i];

        // NEW: Check slot state
        if (slotInfo.state == SlotState::Tentative) {
            std::cout << "[ValidateVariadicInputsImpl] WARNING: Slot " << i
                      << " still in Tentative state (not validated against shader)\n";
            // Allow tentative slots - they'll be validated by derived class
        }

        if (slotInfo.state == SlotState::Invalid) {
            std::cout << "[ValidateVariadicInputsImpl] ERROR: Slot " << i
                      << " marked as Invalid\n";
            return false;
        }

        // Check for null resources
        if (!slotInfo.resource) {
            std::cout << "[ValidateVariadicInputsImpl] ERROR: Slot " << i
                      << " has null resource\n";
            return false;
        }

        // Validate type (if slot is validated/compiled)
        if (slotInfo.state >= SlotState::Validated) {
            if (slotInfo.resource->GetType() != slotInfo.resourceType) {
                std::cout << "[ValidateVariadicInputsImpl] ERROR: Slot " << i
                          << " type mismatch\n";
                return false;
            }
        }
    }

    return true;
}
```

**Affected Files**:
- `RenderGraph/include/Core/VariadicTypedNode.h` - validation logic update

**Estimated Impact**: Small (adds state checking)

---

### Step 7: Testing and Validation

#### 7.1 Test Cases
1. **Basic variadic connection** - single descriptor
2. **Multiple variadic connections** - 3+ descriptors
3. **Type mismatch** - connect wrong resource type (should error in Compile)
4. **Count mismatch** - fewer/more connections than shader expects (should error in Compile)
5. **Hot-reload** - change shader, recompile graph
6. **PreRegisterVariadicSlots** - optional pre-registration still works

#### 7.2 Validation Points
- [ ] Build succeeds without errors
- [ ] DescriptorResourceGathererNode discovers slots during Compile
- [ ] Tentative slots created during ConnectVariadic
- [ ] Validation errors report helpful messages
- [ ] Compute shader executes successfully
- [ ] No crashes or memory leaks

#### 7.3 Debug Output
Add temporary logging to verify phase execution:
```cpp
std::cout << "[Phase] Connection: Created tentative slot 0\n";
std::cout << "[Phase] Setup: DescriptorResourceGathererNode initialized\n";
std::cout << "[Phase] Compile: Validating tentative slots against shader...\n";
std::cout << "[Phase] Compile: Slot 0 validated (storage_image_0)\n";
```

---

## Migration Order

### Phase A: Preparation (Low Risk)
1. Add SlotState enum
2. Update VariadicSlotInfo struct
3. Add UpdateVariadicSlot helper
4. Update documentation

**Test**: Build succeeds, existing code unchanged

### Phase B: Connection Changes (Medium Risk)
1. Modify ConnectVariadic to be optimistic
2. Update VariadicTypedNode validation

**Test**: Existing connections still work

### Phase C: Node Refactoring (Medium Risk)
1. Separate DescriptorResourceGathererNode Setup/Compile
2. Add ValidateTentativeSlotsAgainstShader
3. Remove GraphCompileSetup override

**Test**: Descriptor gathering still works

### Phase D: Cleanup (Low Risk)
1. Remove PreRegisterVariadicSlots call from VulkanGraphApplication
2. Simplify user-facing API
3. Update memory bank documentation

**Test**: Full integration test

### Phase E: Validation (Final)
1. Run all test cases
2. Verify debug output
3. Check for memory leaks
4. Performance profiling

---

## Rollback Plan

If migration fails at any phase:

1. **Revert Git commits** - each phase should be a separate commit
2. **Restore original behavior** - keep old code in comments during migration
3. **Document failure reason** - add to migration notes

Each phase is designed to be independently revertable.

---

## Files Affected Summary

### High Impact (Core Changes)
- `RenderGraph/src/Core/RenderGraph.cpp` - ConnectVariadic logic
- `RenderGraph/include/Core/VariadicTypedNode.h` - SlotState, validation
- `RenderGraph/src/Nodes/DescriptorResourceGathererNode.cpp` - Setup/Compile separation

### Medium Impact (API Changes)
- `RenderGraph/include/Core/RenderGraph.h` - ConnectVariadic signature (unchanged but behavior changed)
- `RenderGraph/include/Nodes/DescriptorResourceGathererNode.h` - method signatures

### Low Impact (Usage/Documentation)
- `source/VulkanGraphApplication.cpp` - user code simplification
- `documentation/RenderGraph_Lifecycle_Schema.md` - update with new flow
- `memory-bank/systemPatterns.md` - update architecture notes

---

## Success Criteria

- [ ] Compute shader renders successfully
- [ ] No PreRegisterVariadicSlots workaround needed
- [ ] Setup phase has no input data access
- [ ] Compile phase validates tentative slots
- [ ] Clear error messages on validation failure
- [ ] No performance regression
- [ ] All existing tests pass
- [ ] Documentation updated

---

## Estimated Timeline

- Phase A: 1 hour (prep, low risk)
- Phase B: 2 hours (connection changes, testing)
- Phase C: 3 hours (node refactoring, validation logic)
- Phase D: 1 hour (cleanup, simplification)
- Phase E: 2 hours (integration testing)

**Total**: ~9 hours of focused work

---

## Notes

- Keep old code in comments during migration for easy rollback
- Commit after each phase for granular version control
- Test incrementally - don't wait until end
- Update memory bank after successful migration
- Consider adding phase transition logging for debugging
