#include "Nodes/DescriptorResourceGathererNode.h"
#include "ShaderManagement/ShaderDataBundle.h"
#include "VulkanSwapChain.h"  // For SwapChainPublicVariables
#include "Core/RenderGraph.h"
#include <iostream>

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
}

// GraphCompileSetup removed - cannot access connected inputs during this phase
// Descriptor discovery happens in SetupImpl via DiscoverDescriptors(ctx)

void DescriptorResourceGathererNode::SetupImpl(Context& ctx) {
    std::cout << "[DescriptorResourceGathererNode::Setup] Node initialization (no data access)\n";
    // Phase C: Setup is now node initialization only
    // No input data access, no slot discovery
    // Tentative slots already created by ConnectVariadic
}

void DescriptorResourceGathererNode::CompileImpl(Context& ctx) {
    std::cout << "[DescriptorResourceGathererNode::Compile] Validating tentative slots against shader metadata...\n";

    // Get shader bundle to discover expected descriptor layout
    auto shaderBundle = ctx.In(DescriptorResourceGathererNodeConfig::SHADER_DATA_BUNDLE);
    if (!shaderBundle || !shaderBundle->descriptorLayout) {
        std::cout << "[DescriptorResourceGathererNode::Compile] ERROR: No shader bundle or descriptor layout\n";
        return;
    }

    const auto* layoutSpec = shaderBundle->descriptorLayout.get();
    std::cout << "[DescriptorResourceGathererNode::Compile] Shader expects " << layoutSpec->bindings.size()
              << " descriptor bindings\n";

    // Validate tentative slots against shader requirements
    ValidateTentativeSlotsAgainstShader(ctx, shaderBundle.get());

    // Call base validation (type checks, null checks)
    if (!ValidateVariadicInputsImpl(ctx)) {
        std::cout << "[DescriptorResourceGathererNode::Compile] ERROR: Variadic input validation failed\n";
        return;
    }

    // Find max binding to size output array
    uint32_t maxBinding = 0;
    for (const auto& binding : layoutSpec->bindings) {
        maxBinding = std::max(maxBinding, binding.binding);
    }
    resourceArray_.resize(maxBinding + 1);

    std::cout << "[DescriptorResourceGathererNode::Compile] Validation complete. Gathering "
              << GetVariadicInputCount() << " resources\n";

    // Gather resources from validated slots
    GatherResources(ctx);

    // Output resource array and pass through shader bundle
    ctx.Out(DescriptorResourceGathererNodeConfig::DESCRIPTOR_RESOURCES, resourceArray_);
    ctx.Out(DescriptorResourceGathererNodeConfig::SHADER_DATA_BUNDLE_OUT, shaderBundle);

    std::cout << "[DescriptorResourceGathererNode::Compile] Output DESCRIPTOR_RESOURCES with "
              << resourceArray_.size() << " entries\n";
}

void DescriptorResourceGathererNode::ExecuteImpl(Context& ctx) {
    // Resources already gathered during Compile phase
    // Nothing to do here
}

void DescriptorResourceGathererNode::CleanupImpl() {
    descriptorSlots_.clear();
    resourceArray_.clear();
}

//-----------------------------------------------------------------------------
// Helper Methods
//-----------------------------------------------------------------------------

void DescriptorResourceGathererNode::ValidateTentativeSlotsAgainstShader(Context& ctx, const ShaderManagement::ShaderDataBundle* shaderBundle) {
    const auto* layoutSpec = shaderBundle->descriptorLayout.get();
    if (!layoutSpec) {
        std::cout << "[DescriptorResourceGathererNode::ValidateTentativeSlots] ERROR: No descriptor layout\n";
        return;
    }

    size_t bundleIndex = 0;
    size_t variadicCount = GetVariadicInputCount(bundleIndex);

    std::cout << "[DescriptorResourceGathererNode::ValidateTentativeSlots] Validating " << variadicCount
              << " tentative slots against " << layoutSpec->bindings.size() << " shader bindings\n";

    RenderGraph* graph = GetOwningGraph();
    if (!graph) {
        std::cout << "[DescriptorResourceGathererNode::ValidateTentativeSlots] ERROR: No owning graph\n";
        return;
    }

    // Validate and update all tentative slots against shader requirements
    for (size_t i = 0; i < variadicCount; ++i) {
        const auto* slotInfo = GetVariadicSlotInfo(i, bundleIndex);
        if (!slotInfo || slotInfo->state != SlotState::Tentative) {
            continue;  // Skip non-tentative slots
        }

        // Find matching shader binding
        bool foundMatch = false;
        for (const auto& shaderBinding : layoutSpec->bindings) {
            if (shaderBinding.binding == slotInfo->binding) {
                // Create updated slot info
                VariadicSlotInfo updatedSlot = *slotInfo;

                // Update descriptor type from shader if mismatch
                if (shaderBinding.descriptorType != slotInfo->descriptorType) {
                    std::cout << "[DescriptorResourceGathererNode::ValidateTentativeSlots] Updating slot " << i
                              << " descriptor type from " << slotInfo->descriptorType
                              << " to " << shaderBinding.descriptorType << " (from shader)\n";
                    updatedSlot.descriptorType = shaderBinding.descriptorType;
                }

                // Keep source info for deferred resource fetch in GatherResources
                // (Don't fetch now - source node might not be compiled yet)

                // Mark as validated
                updatedSlot.state = SlotState::Validated;

                // Update the slot
                UpdateVariadicSlot(i, updatedSlot, bundleIndex);

                std::cout << "[DescriptorResourceGathererNode::ValidateTentativeSlots] Slot " << i
                          << " (binding=" << slotInfo->binding << ") validated and updated (state=Validated)\n";
                foundMatch = true;
                break;
            }
        }

        if (!foundMatch) {
            std::cout << "[DescriptorResourceGathererNode::ValidateTentativeSlots] WARNING: Slot " << i
                      << " (binding=" << slotInfo->binding << ") has no matching shader binding\n";

            // Mark as invalid
            VariadicSlotInfo updatedSlot = *slotInfo;
            updatedSlot.state = SlotState::Invalid;
            UpdateVariadicSlot(i, updatedSlot, bundleIndex);
        }
    }
}

void DescriptorResourceGathererNode::DiscoverDescriptors(Context& ctx) {
    // DEPRECATED - slot discovery moved to Compile phase
    // This method kept for backward compatibility but should not be called
    std::cout << "[DescriptorResourceGathererNode::DiscoverDescriptors] DEPRECATED: Use CompileImpl validation instead\n";
}

bool DescriptorResourceGathererNode::ValidateVariadicInputsImpl(Context& ctx, size_t bundleIndex) {
    // Call base class validation (count constraints, null checks, type validation)
    if (!VariadicTypedNode<DescriptorResourceGathererNodeConfig>::ValidateVariadicInputsImpl(ctx, bundleIndex)) {
        return false;  // Base validation failed
    }

    size_t inputCount = GetVariadicInputCount(bundleIndex);

    // Shader-specific validation: descriptor type matching
    bool allValid = true;
    for (size_t i = 0; i < inputCount; ++i) {
        Resource* res = GetVariadicInputResource(i, bundleIndex);
        const auto* slotInfo = GetVariadicSlotInfo(i, bundleIndex);

        if (!slotInfo) continue;

        VkDescriptorType expectedType = slotInfo->descriptorType;
        if (!ValidateResourceType(res, expectedType)) {
            std::cout << "[DescriptorResourceGathererNode::ValidateVariadicInputsImpl] ERROR: Resource " << i
                      << " (" << slotInfo->slotName << ") type mismatch for shader binding " << slotInfo->binding
                      << " (expected VkDescriptorType=" << expectedType << ")\n";
            allValid = false;
        }
    }

    return allValid;
}

void DescriptorResourceGathererNode::GatherResources(Context& ctx) {
    // Gather validated variadic slots into resource array, indexed by binding
    size_t bundleIndex = 0;
    size_t inputCount = GetVariadicInputCount(bundleIndex);

    std::cout << "[DescriptorResourceGathererNode::GatherResources] Gathering " << inputCount << " validated slots\n";

    for (size_t i = 0; i < inputCount; ++i) {
        const auto* slotInfo = GetVariadicSlotInfo(i, bundleIndex);
        if (!slotInfo) {
            std::cout << "[DescriptorResourceGathererNode::GatherResources] WARNING: Null slot at index " << i << "\n";
            continue;
        }

        // Skip invalid slots (failed validation)
        if (slotInfo->state == SlotState::Invalid) {
            std::cout << "[DescriptorResourceGathererNode::GatherResources] Skipping invalid slot " << i
                      << " (binding=" << slotInfo->binding << ")\n";
            continue;
        }

        // Slot should have valid resource after validation
        if (!slotInfo->resource) {
            std::cout << "[DescriptorResourceGathererNode::GatherResources] WARNING: Validated slot " << i
                      << " (binding=" << slotInfo->binding << ") has null resource\n";
            continue;
        }

        uint32_t binding = slotInfo->binding;
        auto variant = slotInfo->resource->GetHandleVariant();

        std::cout << "[DescriptorResourceGathererNode::GatherResources] Slot " << i << " variant index=" << variant.index()
                  << ", resource type=" << static_cast<int>(slotInfo->resource->GetType())
                  << ", isValid=" << slotInfo->resource->IsValid() << "\n";

        // Check if this is a SwapChainPublicVariables* (special case for per-frame resources)
        if (auto* scPtr = std::get_if<SwapChainPublicVariables*>(&variant)) {
            resourceArray_[binding] = variant;
            std::cout << "[DescriptorResourceGathererNode::GatherResources] Gathered SwapChainPublicVariables* for binding "
                      << binding << " (" << slotInfo->slotName << "), image count=" << (*scPtr)->swapChainImageCount << "\n";
        }
        // Regular resources (must be valid)
        else if (slotInfo->resource->IsValid()) {
            resourceArray_[binding] = variant;
            std::cout << "[DescriptorResourceGathererNode::GatherResources] Gathered resource for binding " << binding
                      << " (" << slotInfo->slotName << "), variant index=" << variant.index()
                      << ", resource type=" << static_cast<int>(slotInfo->resource->GetType()) << "\n";
        }
        else {
            std::cout << "[DescriptorResourceGathererNode::GatherResources] WARNING: Slot " << i
                      << " (binding=" << binding << ", name=" << slotInfo->slotName
                      << ") resource is not valid, variant index=" << variant.index() << "\n";
        }
    }

    std::cout << "[DescriptorResourceGathererNode::GatherResources] Gathered " << resourceArray_.size() << " total resources\n";
}

bool DescriptorResourceGathererNode::ValidateResourceType(Resource* res, VkDescriptorType expectedType) {
    if (!res) return false;

    // Infer actual descriptor type from resource
    VkDescriptorType actualType = InferDescriptorType(res);

    // Check type compatibility
    return actualType == expectedType;
}

VkDescriptorType DescriptorResourceGathererNode::InferDescriptorType(Resource* res) {
    if (!res) return VK_DESCRIPTOR_TYPE_MAX_ENUM;

    // Infer from resource type
    ResourceType resType = res->GetType();

    switch (resType) {
        case ResourceType::Image:
            // Could be storage image, sampled image, or combined sampler
            // Default to storage for now - ideally we'd check descriptor metadata
            return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

        case ResourceType::Buffer:
            // Could be uniform or storage buffer
            // Default to uniform - ideally we'd check descriptor metadata
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

        default:
            return VK_DESCRIPTOR_TYPE_MAX_ENUM;
    }
}

} // namespace Vixen::RenderGraph
