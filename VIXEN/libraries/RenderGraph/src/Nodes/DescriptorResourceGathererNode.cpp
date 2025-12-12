#include "Nodes/DescriptorResourceGathererNode.h"
#include "ShaderDataBundle.h"
#include "SpirvReflectionData.h"  // For DescriptorLayoutSpecification and DescriptorBindingInfo
#include "VulkanSwapChain.h"  // For SwapChainPublicVariables
#include "Core/RenderGraph.h"
#include "Core/NodeLogging.h"
#include "VulkanDevice.h"  // For device limits validation
#include <map>  // For descriptor type counting

namespace Vixen::RenderGraph {

//-----------------------------------------------------------------------------
// DescriptorResourceGathererNodeType Implementation
//-----------------------------------------------------------------------------

std::unique_ptr<NodeInstance> DescriptorResourceGathererNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::make_unique<DescriptorResourceGathererNode>(instanceName, const_cast<DescriptorResourceGathererNodeType*>(this));
}

//-----------------------------------------------------------------------------
// DescriptorResourceGathererNode Implementation
//-----------------------------------------------------------------------------

DescriptorResourceGathererNode::DescriptorResourceGathererNode(
    const std::string& instanceName,
    NodeType* nodeType
) : VariadicTypedNode<DescriptorResourceGathererNodeConfig>(instanceName, nodeType) {
    // Initialize with default variadic constraints from type definition
    auto* descNodeType = static_cast<DescriptorResourceGathererNodeType*>(nodeType);
    SetVariadicInputConstraints(
        descNodeType->GetDefaultMinVariadicInputs(),
        descNodeType->GetDefaultMaxVariadicInputs()
    );
}

// GraphCompileSetup removed - cannot access connected inputs during this phase
// Descriptor discovery happens in SetupImpl via DiscoverDescriptors(ctx)

void DescriptorResourceGathererNode::SetupImpl(VariadicSetupContext& ctx) {
    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::Setup] Node initialization (no data access)");
    // Phase C: Setup is now node initialization only
    // No input data access, no slot discovery
    // Tentative slots already created by ConnectVariadic
}

void DescriptorResourceGathererNode::CompileImpl(VariadicCompileContext& ctx) {
    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::Compile] START for " + GetInstanceName());

    // Get shader bundle to discover expected descriptor layout
    auto shaderBundle = ctx.In(DescriptorResourceGathererNodeConfig::SHADER_DATA_BUNDLE);
    if (!shaderBundle) {
        NODE_LOG_ERROR("[DescriptorResourceGathererNode::Compile] ERROR: No shader bundle for " + GetInstanceName()
                  + " - ensure ShaderLibraryNode is connected via SHADER_DATA_BUNDLE slot");
        return;
    }
    if (!shaderBundle->descriptorLayout) {
        NODE_LOG_ERROR("[DescriptorResourceGathererNode::Compile] ERROR: Shader bundle has no descriptor layout for " + GetInstanceName());
        return;
    }

    const auto* layoutSpec = shaderBundle->descriptorLayout.get();
    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::Compile] Shader expects " + std::to_string(layoutSpec->bindings.size()) + " descriptor bindings");

    // Validate against device limits
    auto* device = this->GetDevice();
    if (device && device->gpu) {
        const auto& limits = device->gpuProperties.limits;

        // Count descriptors by type
        std::map<VkDescriptorType, uint32_t> descriptorCounts;
        for (const auto& binding : layoutSpec->bindings) {
            descriptorCounts[binding.descriptorType] += binding.descriptorCount;
        }

        // Validate each descriptor type against device limits
        for (const auto& [type, count] : descriptorCounts) {
            uint32_t limit = 0;
            const char* typeName = "";

            switch (type) {
                case VK_DESCRIPTOR_TYPE_SAMPLER:
                case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                    limit = limits.maxPerStageDescriptorSamplers;
                    typeName = "Samplers";
                    break;
                case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
                    limit = limits.maxPerStageDescriptorSampledImages;
                    typeName = "Sampled Images";
                    break;
                case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                    limit = limits.maxPerStageDescriptorStorageImages;
                    typeName = "Storage Images";
                    break;
                case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
                    limit = limits.maxPerStageDescriptorUniformBuffers;
                    typeName = "Uniform Buffers";
                    break;
                case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
                    limit = limits.maxPerStageDescriptorStorageBuffers;
                    typeName = "Storage Buffers";
                    break;
                default:
                    continue;  // Skip unknown types
            }

            if (count > limit) {
                NODE_LOG_ERROR("[DescriptorResourceGathererNode::Compile] " + std::string(typeName) +
                             " count " + std::to_string(count) + " exceeds device limit " + std::to_string(limit));
                throw std::runtime_error(std::string(typeName) + " count " + std::to_string(count) +
                                       " exceeds device limit " + std::to_string(limit));
            }

            // Log usage statistics
            float usagePercent = (static_cast<float>(count) / limit) * 100.0f;
            uint32_t remaining = limit - count;
            NODE_LOG_INFO("[DescriptorResourceGathererNode::Compile] " + std::string(typeName) + " usage: " +
                         std::to_string(count) + "/" + std::to_string(limit) +
                         " (" + std::to_string(static_cast<int>(usagePercent)) + "%, " +
                         std::to_string(remaining) + " remaining)");
        }
    }

    // Validate tentative slots against shader requirements
    ValidateTentativeSlotsAgainstShader(ctx, shaderBundle);

    // Call base validation (type checks, null checks)
    if (!ValidateVariadicInputsImpl(ctx)) {
        NODE_LOG_ERROR("[DescriptorResourceGathererNode::Compile] ERROR: Variadic input validation failed for " + GetInstanceName());
        return;
    }
    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::Compile] Validation passed for " + GetInstanceName()
              + ", bindings.size()=" + std::to_string(layoutSpec->bindings.size()));

    // Find max binding to size output array
    uint32_t maxBinding = 0;
    for (const auto& binding : layoutSpec->bindings) {
        maxBinding = std::max(maxBinding, binding.binding);
    }
    resourceArray_.resize(maxBinding + 1);  // Each entry: handle + slotRole + debugCapture

    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::Compile] Validation complete. Gathering " + std::to_string(GetVariadicInputCount()) + " resources");

    // Gather resources from validated slots
    GatherResources(ctx);

    // Debug: Log entries being output
    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::Compile] Outputting resource entries:");
    size_t debugCaptureCount = 0;
    for (size_t i = 0; i < resourceArray_.size(); ++i) {
        uint8_t roleVal = static_cast<uint8_t>(resourceArray_[i].slotRole);
        bool hasDebug = resourceArray_[i].debugCapture != nullptr;
        if (hasDebug) debugCaptureCount++;
        NODE_LOG_DEBUG("  Binding " + std::to_string(i) + ": role=" + std::to_string(roleVal) +
                      (hasDebug ? " [DEBUG]" : ""));
    }

    // Output resource array and pass through shader bundle
    ctx.Out(DescriptorResourceGathererNodeConfig::DESCRIPTOR_RESOURCES, resourceArray_);
    ctx.Out(DescriptorResourceGathererNodeConfig::SHADER_DATA_BUNDLE_OUT, shaderBundle);

    // Extract and output first debug capture for downstream debug reader nodes
    Debug::IDebugCapture* firstDebugCapture = nullptr;
    for (const auto& entry : resourceArray_) {
        if (entry.debugCapture != nullptr) {
            firstDebugCapture = entry.debugCapture;
            NODE_LOG_INFO("[DescriptorResourceGathererNode::Compile] Outputting debug capture: " + firstDebugCapture->GetDebugName());
            break;
        }
    }
    ctx.Out(DescriptorResourceGathererNodeConfig::DEBUG_CAPTURE, firstDebugCapture);

    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::Compile] Output DESCRIPTOR_RESOURCES with " + std::to_string(resourceArray_.size()) + " entries");
    if (debugCaptureCount > 0) {
        NODE_LOG_DEBUG("[DescriptorResourceGathererNode::Compile] " + std::to_string(debugCaptureCount) + " entries have debug capture interfaces");
    }
}

void DescriptorResourceGathererNode::ExecuteImpl(VariadicExecuteContext& ctx) {
    // Execute phase: Update transient (per-frame) resources only
    // - Compile phase gathered static resources and validated against shader
    // - Execute phase refreshes transient resources (like current frame image view)
    // This separation avoids redundant work while supporting frame-varying data

    size_t variadicCount = ctx.InVariadicCount();
    bool hasTransients = false;

    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::Execute] Processing " + std::to_string(variadicCount) + " variadic slots for Execute-role resources");

    for (size_t i = 0; i < variadicCount; ++i) {
        const auto* slotInfo = ctx.InVariadicSlot(i);
        if (!slotInfo) {
            NODE_LOG_DEBUG("[DescriptorResourceGathererNode::Execute] Slot " + std::to_string(i) + " is null, skipping");
            continue;
        }

        // Skip uninitialized slots (created by vector resize, not by ConnectVariadic)
        if (slotInfo->binding == UINT32_MAX) {
            continue;
        }

        uint8_t roleVal = static_cast<uint8_t>(slotInfo->slotRole);
        bool hasExec = HasExecute(slotInfo->slotRole);
        NODE_LOG_DEBUG("[DescriptorResourceGathererNode::Execute] Slot " + std::to_string(i) +
                      " (binding=" + std::to_string(slotInfo->binding) + "): role=" + std::to_string(roleVal) +
                      " (Dependency=" + std::to_string(roleVal & static_cast<uint8_t>(SlotRole::Dependency)) +
                      ", Execute=" + std::to_string(roleVal & static_cast<uint8_t>(SlotRole::Execute)) +
                      "), hasExecute=" + (hasExec ? "YES" : "NO"));

        if (!hasExec) {
            continue;  // Skip Dependency-only slots (already gathered in Compile)
        }

        hasTransients = true;

        // Fetch fresh resource from source node
        NodeInstance* sourceNodeInst = GetOwningGraph()->GetInstance(slotInfo->sourceNode);
        if (!sourceNodeInst) {
            NODE_LOG_DEBUG("[DescriptorResourceGathererNode::Execute] WARNING: Transient slot " + std::to_string(i) + " has invalid source node");
            continue;
        }

        Resource* freshResource = sourceNodeInst->GetOutput(slotInfo->sourceOutput, 0);
        if (!freshResource) {
            NODE_LOG_DEBUG("[DescriptorResourceGathererNode::Execute] WARNING: Transient slot " + std::to_string(i) + " source output is null");
            continue;
        }

        // Update resource entry's handle with fresh value (preserving slotRole and debugCapture)
        uint32_t binding = slotInfo->binding;
        auto variant = freshResource->GetDescriptorHandle();

        // Bounds check - binding must be within resourceArray_ range
        if (binding >= resourceArray_.size()) {
            NODE_LOG_ERROR("[DescriptorResourceGathererNode::Execute] ERROR: Binding " + std::to_string(binding)
                      + " out of range (resourceArray_.size()=" + std::to_string(resourceArray_.size()) + ")");
            continue;
        }

        NODE_LOG_DEBUG("[DescriptorResourceGathererNode::Execute] Updated transient resource at binding " + std::to_string(binding) +
                      " (slot " + std::to_string(i) + "), variant type: " +
                      (std::holds_alternative<std::monostate>(variant) ? "monostate" :
                       std::holds_alternative<VkImageView>(variant) ? "VkImageView" :
                       std::holds_alternative<VkBuffer>(variant) ? "VkBuffer" :
                       std::holds_alternative<VkSampler>(variant) ? "VkSampler" : "unknown"));

        resourceArray_[binding].handle = variant;
    }

    if (hasTransients) {
        // Re-output updated resource array
        ctx.Out(DescriptorResourceGathererNodeConfig::DESCRIPTOR_RESOURCES, resourceArray_);

        // Log what we're outputting
        NODE_LOG_DEBUG("[DescriptorResourceGathererNode::Execute] Re-output DESCRIPTOR_RESOURCES with " + std::to_string(resourceArray_.size()) + " entries (transients updated):");
        for (size_t i = 0; i < resourceArray_.size(); ++i) {
            const auto& entry = resourceArray_[i];
            NODE_LOG_DEBUG("  Binding " + std::to_string(i) + ": " +
                          (std::holds_alternative<std::monostate>(entry.handle) ? "monostate" :
                           std::holds_alternative<VkImageView>(entry.handle) ? "VkImageView" :
                           std::holds_alternative<VkBuffer>(entry.handle) ? "VkBuffer" :
                           std::holds_alternative<VkSampler>(entry.handle) ? "VkSampler" : "unknown"));
        }
    } else {
        NODE_LOG_DEBUG("[DescriptorResourceGathererNode::Execute] No Execute-role resources found - skipping output");
    }

    // Always output debug capture (it may be needed per-frame for readback after dispatch)
    Debug::IDebugCapture* firstDebugCapture = nullptr;
    for (const auto& entry : resourceArray_) {
        if (entry.debugCapture != nullptr) {
            firstDebugCapture = entry.debugCapture;
            break;
        }
    }
    ctx.Out(DescriptorResourceGathererNodeConfig::DEBUG_CAPTURE, firstDebugCapture);
}

void DescriptorResourceGathererNode::CleanupImpl(VariadicCleanupContext& ctx) {
    descriptorSlots_.clear();
    resourceArray_.clear();
}

//-----------------------------------------------------------------------------
// Helper Methods
//-----------------------------------------------------------------------------

void DescriptorResourceGathererNode::ValidateTentativeSlotsAgainstShader(VariadicCompileContext& ctx, const std::shared_ptr<::ShaderManagement::ShaderDataBundle>& shaderBundle) {
    const auto* layoutSpec = shaderBundle->descriptorLayout.get();
    if (!layoutSpec) {
        NODE_LOG_INFO("[DescriptorResourceGathererNode::ValidateTentativeSlots] ERROR: No descriptor layout");
        return;
    }

    size_t variadicCount = ctx.InVariadicCount();
    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::ValidateTentativeSlots] Validating " + std::to_string(variadicCount) + " tentative slots against " + std::to_string(layoutSpec->bindings.size()) + " shader bindings");

    if (!GetOwningGraph()) {
        NODE_LOG_INFO("[DescriptorResourceGathererNode::ValidateTentativeSlots] ERROR: No owning graph");
        return;
    }

    // Validate and update all tentative slots against shader requirements
    for (size_t i = 0; i < variadicCount; ++i) {
        const auto* slotInfo = ctx.InVariadicSlot(i);
        if (!slotInfo || slotInfo->binding == UINT32_MAX || slotInfo->state != SlotState::Tentative) {
            continue;  // Skip null, uninitialized, or non-tentative slots
        }

        ValidateSingleSlotAgainstShader(ctx, i, slotInfo, layoutSpec);
    }
}

void DescriptorResourceGathererNode::ValidateSingleSlotAgainstShader(
    VariadicCompileContext& ctx,
    size_t slotIndex,
    const VariadicSlotInfo* slotInfo,
    const ::ShaderManagement::DescriptorLayoutSpec* layoutSpec
) {
    // Find matching shader binding
    for (const auto& shaderBinding : layoutSpec->bindings) {
        if (shaderBinding.binding == slotInfo->binding) {
            UpdateSlotWithShaderBinding(ctx, slotIndex, slotInfo, shaderBinding);
            return;
        }
    }

    // No matching shader binding found - mark as invalid
    MarkSlotAsInvalid(ctx, slotIndex, slotInfo);
}

void DescriptorResourceGathererNode::UpdateSlotWithShaderBinding(
    VariadicCompileContext& ctx,
    size_t slotIndex,
    const VariadicSlotInfo* slotInfo,
    const ::ShaderManagement::DescriptorBindingSpec& shaderBinding
) {
    VariadicSlotInfo updatedSlot = *slotInfo;

    // Update descriptor type from shader if mismatch
    if (shaderBinding.descriptorType != slotInfo->descriptorType) {
        NODE_LOG_DEBUG("[DescriptorResourceGathererNode::UpdateSlotWithShaderBinding] Updating slot " + std::to_string(slotIndex) + " descriptor type from " + std::to_string(slotInfo->descriptorType) + " to " + std::to_string(shaderBinding.descriptorType) + " (from shader)");
        updatedSlot.descriptorType = shaderBinding.descriptorType;
    }

    // Mark as validated
    updatedSlot.state = SlotState::Validated;

    // Update the slot via context
    ctx.UpdateVariadicSlot(slotIndex, updatedSlot);

    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::UpdateSlotWithShaderBinding] Slot " + std::to_string(slotIndex) + " (binding=" + std::to_string(slotInfo->binding) + ") validated and updated (state=Validated)");
}

void DescriptorResourceGathererNode::MarkSlotAsInvalid(
    VariadicCompileContext& ctx,
    size_t slotIndex,
    const VariadicSlotInfo* slotInfo
) {
    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::MarkSlotAsInvalid] WARNING: Slot " + std::to_string(slotIndex) + " (binding=" + std::to_string(slotInfo->binding) + ") has no matching shader binding");

    VariadicSlotInfo updatedSlot = *slotInfo;
    updatedSlot.state = SlotState::Invalid;
    ctx.UpdateVariadicSlot(slotIndex, updatedSlot);
}

void DescriptorResourceGathererNode::DiscoverDescriptors(VariadicCompileContext& ctx) {
    // DEPRECATED - slot discovery moved to Compile phase
    // This method kept for backward compatibility but should not be called
    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::DiscoverDescriptors] DEPRECATED: Use CompileImpl validation instead");
}

bool DescriptorResourceGathererNode::ValidateVariadicInputsImpl(VariadicCompileContext& ctx) {
    // NOTE: Base class ValidateVariadicInputsImpl is only available for ExecuteContext
    // Since we're validating at Compile time, we do our own validation here
    // All bundle access goes through context - bundle index handled automatically

    size_t inputCount = ctx.InVariadicCount();
    bool allValid = true;

    NODE_LOG_DEBUG("[ValidateVariadicInputsImpl] Checking " + std::to_string(inputCount) + " slots for " + GetInstanceName());
    for (size_t i = 0; i < inputCount; ++i) {
        const auto* slotInfo = ctx.InVariadicSlot(i);
        NODE_LOG_DEBUG("  slot[" + std::to_string(i) + "]: binding=" + std::to_string(slotInfo ? slotInfo->binding : UINT32_MAX)
                  + " name='" + std::string(slotInfo ? slotInfo->slotName : "NULL") + "'"
                  + " descType=" + std::to_string(slotInfo ? (int)slotInfo->descriptorType : -1)
                  + " state=" + std::to_string(slotInfo ? (int)slotInfo->state : -1)
                  + " role=" + std::to_string(slotInfo ? static_cast<int>(slotInfo->slotRole) : -1)
                  + " hasFieldExtract=" + std::to_string(slotInfo ? slotInfo->hasFieldExtraction : false));
        if (!ValidateSingleInput(ctx, i)) {
            allValid = false;
        }
    }

    return allValid;
}

bool DescriptorResourceGathererNode::ValidateSingleInput(VariadicCompileContext& ctx, size_t slotIndex) {
    const auto* slotInfo = ctx.InVariadicSlot(slotIndex);
    if (!slotInfo) {
        return true;  // Skip null slots
    }

    // Skip uninitialized slots (created by vector resize, not by ConnectVariadic)
    if (slotInfo->binding == UINT32_MAX) {
        return true;  // Uninitialized slots are expected to be skipped
    }

    // Skip Invalid slots (already marked as failed during ValidateTentativeSlotsAgainstShader)
    if (slotInfo->state == SlotState::Invalid) {
        return true;  // Invalid slots are expected to be skipped, not cause validation failure
    }

    // Skip validation for transient slots (Execute) - validated in Execute phase
    if (ShouldSkipTransientSlot(slotInfo, slotIndex)) {
        return true;
    }

    // Skip type validation for field extraction - DescriptorSetNode handles per-frame indexing
    if (ShouldSkipFieldExtractionSlot(slotInfo, slotIndex)) {
        return true;
    }

    // Skip slots with empty name and no resource - these are placeholder slots from
    // incomplete wiring that should not cause validation failure
    Resource* res = ctx.InVariadicResource(slotIndex);
    if (!res && slotInfo->slotName.empty()) {
        NODE_LOG_DEBUG("[ValidateSingleInput] Skipping empty placeholder slot " +
                      std::to_string(slotIndex) + " at binding " + std::to_string(slotInfo->binding));
        return true;
    }

    // Validate resource type against expected descriptor type
    VkDescriptorType expectedType = slotInfo->descriptorType;

    if (!ValidateResourceType(res, expectedType)) {
        NODE_LOG_ERROR("[ValidateSingleInput] FAILED: slot " + std::to_string(slotIndex)
                  + " (" + slotInfo->slotName + ") binding=" + std::to_string(slotInfo->binding)
                  + " expectedType=" + std::to_string(expectedType) + " resource=" + std::string(res ? "valid" : "NULL"));
        return false;
    }

    return true;
}

bool DescriptorResourceGathererNode::ShouldSkipTransientSlot(const VariadicSlotInfo* slotInfo, size_t slotIndex) {
    if (HasExecute(slotInfo->slotRole)) {
        NODE_LOG_DEBUG("[DescriptorResourceGathererNode::ShouldSkipTransientSlot] Skipping transient slot " + std::to_string(slotIndex) + " (" + slotInfo->slotName + ") - will be validated in Execute phase");
        return true;
    }
    return false;
}

bool DescriptorResourceGathererNode::ShouldSkipFieldExtractionSlot(const VariadicSlotInfo* slotInfo, size_t slotIndex) {
    if (slotInfo->hasFieldExtraction) {
        NODE_LOG_DEBUG("[DescriptorResourceGathererNode::ShouldSkipFieldExtractionSlot] Skipping type validation for field extraction slot " + std::to_string(slotIndex) + " (" + slotInfo->slotName + ") - downstream node will handle per-frame extraction");
        return true;
    }
    return false;
}

void DescriptorResourceGathererNode::LogTypeValidationError(size_t slotIndex, const VariadicSlotInfo* slotInfo, VkDescriptorType expectedType) {
    NODE_LOG_INFO("[DescriptorResourceGathererNode::LogTypeValidationError] ERROR: Resource " + std::to_string(slotIndex) + " (" + slotInfo->slotName + ") type mismatch for shader binding " + std::to_string(slotInfo->binding) + " (expected VkDescriptorType=" + std::to_string(expectedType) + ")");
}

void DescriptorResourceGathererNode::GatherResources(VariadicCompileContext& ctx) {
    // Gather validated variadic slots into resource array, indexed by binding
    size_t inputCount = ctx.InVariadicCount();
    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::GatherResources] Gathering " + std::to_string(inputCount) + " validated slots");

    for (size_t i = 0; i < inputCount; ++i) {
        const auto* slotInfo = ctx.InVariadicSlot(i);
        if (!slotInfo || !ProcessSlot(i, slotInfo)) {
            continue;
        }
    }

    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::GatherResources] Gathered " + std::to_string(resourceArray_.size()) + " total resources");
}

bool DescriptorResourceGathererNode::ProcessSlot(size_t slotIndex, const VariadicSlotInfo* slotInfo) {
    if (!slotInfo) {
        NODE_LOG_DEBUG("[DescriptorResourceGathererNode::ProcessSlot] WARNING: Null slot at index " + std::to_string(slotIndex));
        return false;
    }

    // Skip uninitialized slots (created by vector resize, not by ConnectVariadic)
    // These have binding = UINT32_MAX as sentinel value
    if (slotInfo->binding == UINT32_MAX) {
        NODE_LOG_DEBUG("[DescriptorResourceGathererNode::ProcessSlot] Skipping uninitialized slot " + std::to_string(slotIndex));
        return false;
    }

    // Skip invalid slots (failed validation)
    if (slotInfo->state == SlotState::Invalid) {
        NODE_LOG_DEBUG("[DescriptorResourceGathererNode::ProcessSlot] Skipping invalid slot " + std::to_string(slotIndex) + " (binding=" + std::to_string(slotInfo->binding) + ")");
        return false;
    }

    uint32_t binding = slotInfo->binding;
    resourceArray_[binding].slotRole = slotInfo->slotRole;

    // Handle Execute-only slots
    if (!HasDependency(slotInfo->slotRole)) {
        InitializeExecuteOnlySlot(slotIndex, binding, slotInfo->slotRole);
        return true;
    }

    // Validate resource availability
    if (!slotInfo->resource) {
        NODE_LOG_DEBUG("[DescriptorResourceGathererNode::ProcessSlot] WARNING: Validated slot " + std::to_string(slotIndex) + " (binding=" + std::to_string(binding) + ") has null resource");
        return false;
    }

    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::ProcessSlot] Slot " + std::to_string(slotIndex) + " resource type=" + std::to_string(static_cast<int>(slotInfo->resource->GetType())) + ", isValid=" + std::to_string(slotInfo->resource->IsValid()) + ", hasFieldExtraction=" + std::to_string(slotInfo->hasFieldExtraction));

    // Store resource (field extraction or regular)
    if (slotInfo->hasFieldExtraction && slotInfo->fieldOffset != 0) {
        StoreFieldExtractionResource(slotIndex, binding, slotInfo->fieldOffset, slotInfo->resource);
    } else {
        StoreRegularResource(slotIndex, binding, slotInfo->slotName, slotInfo->slotRole, slotInfo->resource);
    }

    // Check for Debug role - attach IDebugCapture interface to the entry if present
    if (HasDebug(slotInfo->slotRole)) {
        // Try to get IDebugCapture interface from the resource
        if (auto* debugCapture = slotInfo->resource->GetInterface<Debug::IDebugCapture>()) {
            resourceArray_[binding].debugCapture = debugCapture;
            NODE_LOG_DEBUG("[DescriptorResourceGathererNode::ProcessSlot] Attached debug capture to binding " +
                          std::to_string(binding) + " (name=" + debugCapture->GetDebugName() + ")");
        } else {
            NODE_LOG_DEBUG("[DescriptorResourceGathererNode::ProcessSlot] WARNING: Debug-flagged slot " +
                          std::to_string(slotIndex) + " does not implement IDebugCapture");
        }
    }

    return true;
}

void DescriptorResourceGathererNode::InitializeExecuteOnlySlot(size_t slotIndex, uint32_t binding, SlotRole role) {
    // Initialize placeholder entry to prevent accessing uninitialized memory
    // This ensures resourceArray_[binding] exists even before Execute phase
    // slotRole already set by ProcessSlot, just initialize handle
    resourceArray_[binding].handle = std::monostate{};
    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::InitializeExecuteOnlySlot] Recorded role for Execute-only slot " + std::to_string(slotIndex) + " (binding=" + std::to_string(binding) + ", role=" + std::to_string(static_cast<uint8_t>(role)) + ") - placeholder initialized, resource will be gathered in Execute phase");
}

void DescriptorResourceGathererNode::StoreFieldExtractionResource(size_t slotIndex, uint32_t binding, size_t fieldOffset, Resource* resource) {
    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::StoreFieldExtractionResource] Extracting field at offset " + std::to_string(fieldOffset) + " from struct for binding " + std::to_string(binding));

    // Extract handle from resource and store in entry
    auto handle = resource->GetDescriptorHandle();

    // Store handle - downstream nodes will handle field extraction if needed
    resourceArray_[binding].handle = handle;

    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::StoreFieldExtractionResource] Stored handle with field at offset " + std::to_string(fieldOffset) + " for binding " + std::to_string(binding) + " (downstream will extract)");
}

template<typename T>
const void* DescriptorResourceGathererNode::ExtractRawPointerFromVariant(T&& structPtr) {
    using StructPtrType = std::decay_t<T>;

    // Get raw pointer from smart pointer or raw pointer
    if constexpr (std::is_pointer_v<StructPtrType>) {
        return const_cast<const void*>(static_cast<const volatile void*>(structPtr));
    } else if constexpr (requires { structPtr.get(); }) {
        return const_cast<const void*>(static_cast<const volatile void*>(structPtr.get()));
    } else {
        NODE_LOG_DEBUG("[DescriptorResourceGathererNode::ExtractRawPointerFromVariant] WARNING: Cannot extract raw pointer from variant type");
        return nullptr;
    }
}

void DescriptorResourceGathererNode::StoreRegularResource(size_t slotIndex, uint32_t binding, const std::string& slotName, SlotRole role, Resource* resource) {
    // Extract handle from resource and store in entry
    auto handle = resource->GetDescriptorHandle();
    resourceArray_[binding].handle = handle;

    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::StoreRegularResource] Gathered resource for binding " + std::to_string(binding) + " (" + slotName + "), variant index=" + std::to_string(handle.index()) + ", role=" + std::to_string(static_cast<int>(role)));
}

bool DescriptorResourceGathererNode::ValidateResourceType(Resource* res, VkDescriptorType expectedType) {
    if (!res) return false;

    // Use visitor pattern to check descriptor compatibility with expected type
    return IsResourceCompatibleWithDescriptorType(res, expectedType);
}

VkDescriptorType DescriptorResourceGathererNode::InferDescriptorType(Resource* res) {
    // Deprecated - kept for legacy compatibility
    // Use IsResourceCompatibleWithDescriptorType instead
    if (!res) return VK_DESCRIPTOR_TYPE_MAX_ENUM;

    ResourceType resType = res->GetType();

    switch (resType) {
        case ResourceType::Image:
            return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        case ResourceType::Buffer:
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        default:
            return VK_DESCRIPTOR_TYPE_MAX_ENUM;
    }
}

bool DescriptorResourceGathererNode::IsResourceCompatibleWithDescriptorType(
    Resource* res,
    VkDescriptorType descriptorType
) {
    if (!res) return false;

    ResourceType resType = res->GetType();
    std::optional<ResourceUsage> usageOpt = ExtractResourceUsage(res);

    // If no usage available, fall back to ResourceType-based compatibility
    if (!usageOpt.has_value()) {
        bool fallbackResult = IsResourceTypeCompatibleWithDescriptor(resType, descriptorType);
        NODE_LOG_DEBUG("[DescriptorResourceGathererNode::IsResourceCompatibleWithDescriptorType] Fallback result for ResourceType=" + std::to_string(static_cast<int>(resType)) + ", VkDescriptorType=" + std::to_string(descriptorType) + ": " + (fallbackResult ? "PASS" : "FAIL"));
        return fallbackResult;
    }

    ResourceUsage usage = usageOpt.value();
    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::IsResourceCompatibleWithDescriptorType] Checking usage=" + std::to_string(static_cast<uint32_t>(usage)) + " against VkDescriptorType=" + std::to_string(descriptorType));

    return CheckUsageCompatibility(usage, resType, descriptorType);
}

std::optional<ResourceUsage> DescriptorResourceGathererNode::ExtractResourceUsage(Resource* res) {
    // Try to extract usage from descriptor using visitor pattern
    if (auto* bufferDesc = res->GetDescriptor<BufferDescriptor>()) {
        NODE_LOG_DEBUG("[DescriptorResourceGathererNode::ExtractResourceUsage] Found BufferDescriptor with usage=" + std::to_string(static_cast<uint32_t>(bufferDesc->usage)));
        return bufferDesc->usage;
    }

    if (auto* imageDesc = res->GetDescriptor<ImageDescriptor>()) {
        NODE_LOG_DEBUG("[DescriptorResourceGathererNode::ExtractResourceUsage] Found ImageDescriptor with usage=" + std::to_string(static_cast<uint32_t>(imageDesc->usage)));
        return imageDesc->usage;
    }

    if (auto* storageImageDesc = res->GetDescriptor<StorageImageDescriptor>()) {
        NODE_LOG_DEBUG("[DescriptorResourceGathererNode::ExtractResourceUsage] Found StorageImageDescriptor");
        return ResourceUsage::Storage;  // Storage images always have Storage usage
    }

    if (auto* texture3DDesc = res->GetDescriptor<Texture3DDescriptor>()) {
        NODE_LOG_DEBUG("[DescriptorResourceGathererNode::ExtractResourceUsage] Found Texture3DDescriptor");
        return ResourceUsage::Sampled;  // 3D textures typically sampled
    }

    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::ExtractResourceUsage] No descriptor with usage found");
    return std::nullopt;
}

bool DescriptorResourceGathererNode::CheckUsageCompatibility(
    ResourceUsage usage,
    ResourceType resType,
    VkDescriptorType descriptorType
) {
    switch (descriptorType) {
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            return HasUsage(usage, ResourceUsage::UniformBuffer);

        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            return HasUsage(usage, ResourceUsage::StorageBuffer);

        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            return HasUsage(usage, ResourceUsage::Storage) &&
                   (resType == ResourceType::Image || resType == ResourceType::StorageImage);

        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            // Both require Sampled usage (combined sampler checks image compatibility)
            return HasUsage(usage, ResourceUsage::Sampled) &&
                   (resType == ResourceType::Image || resType == ResourceType::Image3D);

        case VK_DESCRIPTOR_TYPE_SAMPLER:
            // Samplers are separate resources - check ResourceType
            return resType == ResourceType::Buffer;  // VkSampler registered as Buffer type

        case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
            // Acceleration structures (RTX) - must be AccelerationStructure type
            return resType == ResourceType::AccelerationStructure;

        default:
            // Unknown descriptor type - log error for debugging
            NODE_LOG_ERROR("[CheckUsageCompatibility] ERROR: Unhandled VkDescriptorType=" + std::to_string(descriptorType)
                      + " for ResourceType=" + std::to_string(static_cast<int>(resType))
                      + " with usage=" + std::to_string(static_cast<uint32_t>(usage)));
            return false;
    }
}

bool DescriptorResourceGathererNode::IsResourceTypeCompatibleWithDescriptor(
    ResourceType resType,
    VkDescriptorType descriptorType
) {
    // Fallback compatibility check when usage info not available
    // This handles HandleDescriptor resources (VkImageView, VkSampler, etc.)

    switch (descriptorType) {
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            return resType == ResourceType::Buffer;

        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            return resType == ResourceType::Image ||
                   resType == ResourceType::StorageImage ||
                   resType == ResourceType::Image3D;

        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            // Combined sampler can accept BOTH ImageView (Image type) and Sampler (Buffer type)
            // When two resources connect to same binding, check each individually
            return resType == ResourceType::Image ||
                   resType == ResourceType::StorageImage ||
                   resType == ResourceType::Image3D ||
                   resType == ResourceType::Buffer;  // VkSampler uses Buffer ResourceType

        case VK_DESCRIPTOR_TYPE_SAMPLER:
            return resType == ResourceType::Buffer;  // VkSampler uses Buffer ResourceType

        case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
            // Acceleration structures (RTX) - must be AccelerationStructure type
            return resType == ResourceType::AccelerationStructure;

        default:
            // Unknown descriptor type - log error for debugging
            NODE_LOG_ERROR("[IsResourceTypeCompatibleWithDescriptor] ERROR: Unhandled VkDescriptorType=" + std::to_string(descriptorType)
                      + " for ResourceType=" + std::to_string(static_cast<int>(resType)));
            return false;
    }
}

} // namespace Vixen::RenderGraph
