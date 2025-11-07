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

void DescriptorResourceGathererNode::SetupImpl(VariadicTypedNode<DescriptorResourceGathererNodeConfig>::VariadicSetupContext& ctx) {
    std::cout << "[DescriptorResourceGathererNode::Setup] Node initialization (no data access)\n";
    // Phase C: Setup is now node initialization only
    // No input data access, no slot discovery
    // Tentative slots already created by ConnectVariadic
}

void DescriptorResourceGathererNode::CompileImpl(VariadicTypedNode<DescriptorResourceGathererNodeConfig>::VariadicCompileContext& ctx) {
    std::cout << "[DescriptorResourceGathererNode::Compile] START - Validating tentative slots against shader metadata...\n";
    std::cout << "[DescriptorResourceGathererNode::Compile] Current variadic input count: " << GetVariadicInputCount() << "\n";

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

void DescriptorResourceGathererNode::ExecuteImpl(VariadicTypedNode<DescriptorResourceGathererNodeConfig>::VariadicExecuteContext& ctx) {
    // Execute phase: Update transient (per-frame) resources only
    // - Compile phase gathered static resources and validated against shader
    // - Execute phase refreshes transient resources (like current frame image view)
    // This separation avoids redundant work while supporting frame-varying data

    size_t variadicCount = ctx.InVariadicCount();
    bool hasTransients = false;

    for (size_t i = 0; i < variadicCount; ++i) {
        const auto* slotInfo = ctx.InVariadicSlot(i);
        if (!slotInfo || !(static_cast<uint8_t>(slotInfo->slotRole) & static_cast<uint8_t>(SlotRole::ExecuteOnly))) {
            continue;  // Skip Dependency slots (already gathered in Compile)
        }

        hasTransients = true;

        // Fetch fresh resource from source node
        NodeInstance* sourceNodeInst = GetOwningGraph()->GetInstance(slotInfo->sourceNode);
        if (!sourceNodeInst) {
            std::cout << "[DescriptorResourceGathererNode::Execute] WARNING: Transient slot " << i
                      << " has invalid source node\n";
            continue;
        }

        Resource* freshResource = sourceNodeInst->GetOutput(slotInfo->sourceOutput, 0);
        if (!freshResource) {
            std::cout << "[DescriptorResourceGathererNode::Execute] WARNING: Transient slot " << i
                      << " source output is null\n";
            continue;
        }

        // Update resource array with fresh value
        uint32_t binding = slotInfo->binding;
        auto variant = freshResource->GetHandleVariant();
        resourceArray_[binding] = variant;

        std::cout << "[DescriptorResourceGathererNode::Execute] Updated transient resource at binding "
                  << binding << " (slot " << i << ")\n";
    }

    if (hasTransients) {
        // Re-output updated resource array
        ctx.Out(DescriptorResourceGathererNodeConfig::DESCRIPTOR_RESOURCES, resourceArray_);
        std::cout << "[DescriptorResourceGathererNode::Execute] Re-output DESCRIPTOR_RESOURCES with "
                  << resourceArray_.size() << " entries (transients updated)\n";
    }
}

void DescriptorResourceGathererNode::CleanupImpl(VariadicTypedNode<DescriptorResourceGathererNodeConfig>::VariadicCleanupContext& ctx) {
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

        // Skip validation for transient slots (ExecuteOnly) - validated in Execute phase
        if (static_cast<uint8_t>(slotInfo->slotRole) & static_cast<uint8_t>(SlotRole::ExecuteOnly)) {
            std::cout << "[DescriptorResourceGathererNode::ValidateVariadicInputsImpl] Skipping transient slot "
                      << i << " (" << slotInfo->slotName << ") - will be validated in Execute phase\n";
            continue;
        }

        // Skip type validation for field extraction - DescriptorSetNode handles per-frame indexing
        if (slotInfo->hasFieldExtraction) {
            std::cout << "[DescriptorResourceGathererNode::ValidateVariadicInputsImpl] Skipping type validation for field extraction slot "
                      << i << " (" << slotInfo->slotName << ") - downstream node will handle per-frame extraction\n";
            continue;
        }

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

        // For transient slots (marked ExecuteOnly), fetch resource from source node
        // These don't exist during Compile phase, so skip them here (will be fetched in Execute)
        if (static_cast<uint8_t>(slotInfo->slotRole) & static_cast<uint8_t>(SlotRole::ExecuteOnly)) {
            std::cout << "[DescriptorResourceGathererNode::GatherResources] Skipping transient slot " << i
                      << " (binding=" << slotInfo->binding << ") - will fetch in Execute phase\n";
            continue;
        }

        // Slot should have valid resource after validation (for non-transient)
        if (!slotInfo->resource) {
            std::cout << "[DescriptorResourceGathererNode::GatherResources] WARNING: Validated slot " << i
                      << " (binding=" << slotInfo->binding << ") has null resource\n";
            continue;
        }

        uint32_t binding = slotInfo->binding;
        auto variant = slotInfo->resource->GetHandleVariant();

        std::cout << "[DescriptorResourceGathererNode::GatherResources] Slot " << i << " variant index=" << variant.index()
                  << ", resource type=" << static_cast<int>(slotInfo->resource->GetType())
                  << ", isValid=" << slotInfo->resource->IsValid()
                  << ", hasFieldExtraction=" << slotInfo->hasFieldExtraction << "\n";

        // Handle field extraction from struct using runtime type dispatch
        if (slotInfo->hasFieldExtraction && slotInfo->fieldOffset != 0) {
            std::cout << "[DescriptorResourceGathererNode::GatherResources] Extracting field at offset "
                      << slotInfo->fieldOffset << " from struct for binding " << binding << "\n";

            // Extract field using visitor pattern - get raw struct pointer from variant
            std::visit([&](auto&& structPtr) {
                using StructPtrType = std::decay_t<decltype(structPtr)>;

                // Get raw pointer from smart pointer or raw pointer
                const void* rawStructPtr = nullptr;
                if constexpr (std::is_pointer_v<StructPtrType>) {
                    rawStructPtr = const_cast<const void*>(static_cast<const volatile void*>(structPtr));
                } else if constexpr (requires { structPtr.get(); }) {
                    rawStructPtr = const_cast<const void*>(static_cast<const volatile void*>(structPtr.get()));
                } else {
                    std::cout << "[DescriptorResourceGathererNode::GatherResources] WARNING: Cannot extract raw pointer from variant type\n";
                    return;
                }

                if (!rawStructPtr) {
                    std::cout << "[DescriptorResourceGathererNode::GatherResources] WARNING: Null struct pointer\n";
                    return;
                }

                // Apply field offset to get field pointer
                auto* fieldPtr = reinterpret_cast<const char*>(rawStructPtr) + slotInfo->fieldOffset;

                // Extract field value based on resourceType - dispatch to correct type
                // NOTE: This requires knowing the actual field type at runtime
                // For now, we store the struct and let downstream nodes handle extraction
                resourceArray_[binding] = variant;  // Store original struct

                std::cout << "[DescriptorResourceGathererNode::GatherResources] Stored struct with field at offset "
                          << slotInfo->fieldOffset << " for binding " << binding << " (downstream will extract)\n";
            }, variant);
        }
        // Regular resources - store directly at binding index
        else {
            resourceArray_[binding] = variant;
            std::cout << "[DescriptorResourceGathererNode::GatherResources] Gathered resource for binding " << binding
                      << " (" << slotInfo->slotName << "), variant index=" << variant.index() << "\n";
        }
    }

    std::cout << "[DescriptorResourceGathererNode::GatherResources] Gathered " << resourceArray_.size() << " total resources\n";
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

    // Get descriptor from resource
    ResourceType resType = res->GetType();

    // Try to extract usage from descriptor using visitor pattern
    std::optional<ResourceUsage> usageOpt;

    // Visit BufferDescriptor
    if (auto* bufferDesc = res->GetDescriptor<BufferDescriptor>()) {
        usageOpt = bufferDesc->usage;
        std::cout << "[DescriptorResourceGathererNode::IsResourceCompatibleWithDescriptorType] Found BufferDescriptor with usage="
                  << static_cast<uint32_t>(bufferDesc->usage) << "\n";
    }
    // Visit ImageDescriptor
    else if (auto* imageDesc = res->GetDescriptor<ImageDescriptor>()) {
        usageOpt = imageDesc->usage;
        std::cout << "[DescriptorResourceGathererNode::IsResourceCompatibleWithDescriptorType] Found ImageDescriptor with usage="
                  << static_cast<uint32_t>(imageDesc->usage) << "\n";
    }
    // Visit StorageImageDescriptor
    else if (auto* storageImageDesc = res->GetDescriptor<StorageImageDescriptor>()) {
        usageOpt = ResourceUsage::Storage;  // Storage images always have Storage usage
        std::cout << "[DescriptorResourceGathererNode::IsResourceCompatibleWithDescriptorType] Found StorageImageDescriptor\n";
    }
    // Visit Texture3DDescriptor
    else if (auto* texture3DDesc = res->GetDescriptor<Texture3DDescriptor>()) {
        usageOpt = ResourceUsage::Sampled;  // 3D textures typically sampled
        std::cout << "[DescriptorResourceGathererNode::IsResourceCompatibleWithDescriptorType] Found Texture3DDescriptor\n";
    }
    else {
        std::cout << "[DescriptorResourceGathererNode::IsResourceCompatibleWithDescriptorType] No descriptor with usage found, using fallback\n";
    }

    // If no usage available, fall back to ResourceType-based compatibility
    if (!usageOpt.has_value()) {
        bool fallbackResult = IsResourceTypeCompatibleWithDescriptor(resType, descriptorType);
        std::cout << "[DescriptorResourceGathererNode::IsResourceCompatibleWithDescriptorType] Fallback result for ResourceType="
                  << static_cast<int>(resType) << ", VkDescriptorType=" << descriptorType << ": " << (fallbackResult ? "PASS" : "FAIL") << "\n";
        return fallbackResult;
    }

    ResourceUsage usage = usageOpt.value();
    std::cout << "[DescriptorResourceGathererNode::IsResourceCompatibleWithDescriptorType] Checking usage=" << static_cast<uint32_t>(usage)
              << " against VkDescriptorType=" << descriptorType << "\n";

    // Check compatibility based on descriptor type
    switch (descriptorType) {
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            return HasUsage(usage, ResourceUsage::UniformBuffer);

        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            return HasUsage(usage, ResourceUsage::StorageBuffer);

        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            return HasUsage(usage, ResourceUsage::Storage) &&
                   (resType == ResourceType::Image || resType == ResourceType::StorageImage);

        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            return HasUsage(usage, ResourceUsage::Sampled) &&
                   (resType == ResourceType::Image || resType == ResourceType::Image3D);

        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            // Combined sampler requires Sampled usage
            // Note: Actual sampler is separate resource, we check image compatibility here
            return HasUsage(usage, ResourceUsage::Sampled) &&
                   (resType == ResourceType::Image || resType == ResourceType::Image3D);

        case VK_DESCRIPTOR_TYPE_SAMPLER:
            // Samplers are separate resources - check ResourceType
            return resType == ResourceType::Buffer;  // VkSampler registered as Buffer type

        default:
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

        default:
            return false;
    }
}

} // namespace Vixen::RenderGraph
