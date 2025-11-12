#include "Nodes/DescriptorResourceGathererNode.h"
#include "ShaderManagement/ShaderDataBundle.h"
#include "VulkanSwapChain.h"  // For SwapChainPublicVariables
#include "Core/RenderGraph.h"
#include "Core/NodeLogging.h"

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

void DescriptorResourceGathererNode::SetupImpl(VariadicSetupContext& ctx) {
    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::Setup] Node initialization (no data access)");
    // Phase C: Setup is now node initialization only
    // No input data access, no slot discovery
    // Tentative slots already created by ConnectVariadic
}

void DescriptorResourceGathererNode::CompileImpl(VariadicCompileContext& ctx) {
    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::Compile] START - Validating tentative slots against shader metadata...");
    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::Compile] Current variadic input count: " + std::to_string(GetVariadicInputCount()));

    // Get shader bundle to discover expected descriptor layout
    auto shaderBundle = ctx.In(DescriptorResourceGathererNodeConfig::SHADER_DATA_BUNDLE);
    if (!shaderBundle || !shaderBundle->descriptorLayout) {
        NODE_LOG_INFO("[DescriptorResourceGathererNode::Compile] ERROR: No shader bundle or descriptor layout");
        return;
    }

    const auto* layoutSpec = shaderBundle->descriptorLayout.get();
    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::Compile] Shader expects " + std::to_string(layoutSpec->bindings.size()) + " descriptor bindings");

    // Validate tentative slots against shader requirements
    ValidateTentativeSlotsAgainstShader(ctx, shaderBundle.get());

    // Call base validation (type checks, null checks)
    if (!ValidateVariadicInputsImpl(ctx)) {
        NODE_LOG_INFO("[DescriptorResourceGathererNode::Compile] ERROR: Variadic input validation failed");
        return;
    }

    // Phase H: Find max binding and validate against MAX_DESCRIPTOR_BINDINGS
    uint32_t maxBinding = 0;
    for (const auto& binding : layoutSpec->bindings) {
        maxBinding = std::max(maxBinding, binding.binding);
    }
    descriptorCount_ = maxBinding + 1;

    if (descriptorCount_ > MAX_DESCRIPTOR_BINDINGS) {
        throw std::runtime_error("[DescriptorResourceGathererNode::Compile] Descriptor count (" +
                                 std::to_string(descriptorCount_) +
                                 ") exceeds MAX_DESCRIPTOR_BINDINGS (" +
                                 std::to_string(MAX_DESCRIPTOR_BINDINGS) + ")");
    }

    // Initialize slot roles to Dependency (default)
    for (uint32_t i = 0; i < descriptorCount_; ++i) {
        slotRoleArray_[i] = SlotRole::Dependency;
    }

    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::Compile] Validation complete. Gathering " + std::to_string(GetVariadicInputCount()) + " resources");

    // Gather resources from validated slots
    GatherResources(ctx);

    // Debug: Log slot roles being output
    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::Compile] Outputting slot roles:");
    for (uint32_t i = 0; i < descriptorCount_; ++i) {
        uint8_t roleVal = static_cast<uint8_t>(slotRoleArray_[i]);
        NODE_LOG_DEBUG("  Binding " + std::to_string(i) + ": role=" + std::to_string(roleVal));
    }

    // Phase H: Track stack arrays with URM before output
    TrackStackArray(resourceArray_, descriptorCount_, ResourceLifetime::GraphLocal);
    TrackStackArray(slotRoleArray_, descriptorCount_, ResourceLifetime::GraphLocal);

    // Output resource arrays (convert to vector for interface compatibility)
    std::vector<ResourceVariant> resourceVec(resourceArray_.begin(), resourceArray_.begin() + descriptorCount_);
    std::vector<SlotRole> slotRoleVec(slotRoleArray_.begin(), slotRoleArray_.begin() + descriptorCount_);

    ctx.Out(DescriptorResourceGathererNodeConfig::DESCRIPTOR_RESOURCES, resourceVec);
    ctx.Out(DescriptorResourceGathererNodeConfig::DESCRIPTOR_SLOT_ROLES, slotRoleVec);
    ctx.Out(DescriptorResourceGathererNodeConfig::SHADER_DATA_BUNDLE_OUT, shaderBundle);

    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::Compile] Output DESCRIPTOR_RESOURCES with " + std::to_string(descriptorCount_) + " entries (URM tracked)");
}

void DescriptorResourceGathererNode::ExecuteImpl(VariadicExecuteContext& ctx) {
    // Execute phase: Update transient (per-frame) resources only
    // - Compile phase gathered static resources and validated against shader
    // - Execute phase refreshes transient resources (like current frame image view)
    // This separation avoids redundant work while supporting frame-varying data

    size_t variadicCount = ctx.InVariadicCount();
    bool hasTransients = false;

    for (size_t i = 0; i < variadicCount; ++i) {
        const auto* slotInfo = ctx.InVariadicSlot(i);
        if (!slotInfo || !HasExecute(slotInfo->slotRole)) {
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

        // Update resource array with fresh value
        uint32_t binding = slotInfo->binding;
        auto variant = freshResource->GetHandleVariant();
        resourceArray_[binding] = variant;

        NODE_LOG_DEBUG("[DescriptorResourceGathererNode::Execute] Updated transient resource at binding " + std::to_string(binding) + " (slot " + std::to_string(i) + ")");
    }

    if (hasTransients) {
        // Phase H: Track and re-output updated resource array
        TrackStackArray(resourceArray_, descriptorCount_, ResourceLifetime::FrameLocal);

        std::vector<ResourceVariant> resourceVec(resourceArray_.begin(), resourceArray_.begin() + descriptorCount_);
        ctx.Out(DescriptorResourceGathererNodeConfig::DESCRIPTOR_RESOURCES, resourceVec);
        NODE_LOG_DEBUG("[DescriptorResourceGathererNode::Execute] Re-output DESCRIPTOR_RESOURCES with " + std::to_string(descriptorCount_) + " entries (transients updated, URM tracked)");
    }
}

void DescriptorResourceGathererNode::CleanupImpl(VariadicCleanupContext& ctx) {
    descriptorSlots_.clear();

    // Phase H: Reset array count instead of clear (arrays stay on stack)
    descriptorCount_ = 0;
    // Note: Arrays remain allocated on stack, just reset tracking
}

//-----------------------------------------------------------------------------
// Helper Methods
//-----------------------------------------------------------------------------

void DescriptorResourceGathererNode::ValidateTentativeSlotsAgainstShader(VariadicCompileContext& ctx, const ShaderManagement::ShaderDataBundle* shaderBundle) {
    const auto* layoutSpec = shaderBundle->descriptorLayout.get();
    if (!layoutSpec) {
        NODE_LOG_INFO("[DescriptorResourceGathererNode::ValidateTentativeSlots] ERROR: No descriptor layout");
        return;
    }

    // All bundle access goes through context
    size_t variadicCount = ctx.InVariadicCount();

    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::ValidateTentativeSlots] Validating " + std::to_string(variadicCount) + " tentative slots against " + std::to_string(layoutSpec->bindings.size()) + " shader bindings");

    RenderGraph* graph = GetOwningGraph();
    if (!graph) {
        NODE_LOG_INFO("[DescriptorResourceGathererNode::ValidateTentativeSlots] ERROR: No owning graph");
        return;
    }

    // Validate and update all tentative slots against shader requirements
    for (size_t i = 0; i < variadicCount; ++i) {
        const auto* slotInfo = ctx.InVariadicSlot(i);
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
                    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::ValidateTentativeSlots] Updating slot " + std::to_string(i) + " descriptor type from " + std::to_string(slotInfo->descriptorType) + " to " + std::to_string(shaderBinding.descriptorType) + " (from shader)");
                    updatedSlot.descriptorType = shaderBinding.descriptorType;
                }

                // Keep source info for deferred resource fetch in GatherResources
                // (Don't fetch now - source node might not be compiled yet)

                // Mark as validated
                updatedSlot.state = SlotState::Validated;

                // Update the slot via context
                ctx.UpdateVariadicSlot(i, updatedSlot);

                NODE_LOG_DEBUG("[DescriptorResourceGathererNode::ValidateTentativeSlots] Slot " + std::to_string(i) + " (binding=" + std::to_string(slotInfo->binding) + ") validated and updated (state=Validated)");
                foundMatch = true;
                break;
            }
        }

        if (!foundMatch) {
            NODE_LOG_DEBUG("[DescriptorResourceGathererNode::ValidateTentativeSlots] WARNING: Slot " + std::to_string(i) + " (binding=" + std::to_string(slotInfo->binding) + ") has no matching shader binding");

            // Mark as invalid via context
            VariadicSlotInfo updatedSlot = *slotInfo;
            updatedSlot.state = SlotState::Invalid;
            ctx.UpdateVariadicSlot(i, updatedSlot);
        }
    }
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

    // Shader-specific validation: descriptor type matching
    bool allValid = true;
    for (size_t i = 0; i < inputCount; ++i) {
        const auto* slotInfo = ctx.InVariadicSlot(i);

        // Access resource through context
        Resource* res = ctx.InVariadicResource(i);

        if (!slotInfo) continue;

        // Skip validation for transient slots (Execute) - validated in Execute phase
        if (HasExecute(slotInfo->slotRole)) {
            NODE_LOG_DEBUG("[DescriptorResourceGathererNode::ValidateVariadicInputsImpl] Skipping transient slot " + std::to_string(i) + " (" + slotInfo->slotName + ") - will be validated in Execute phase");
            continue;
        }

        // Skip type validation for field extraction - DescriptorSetNode handles per-frame indexing
        if (slotInfo->hasFieldExtraction) {
            NODE_LOG_DEBUG("[DescriptorResourceGathererNode::ValidateVariadicInputsImpl] Skipping type validation for field extraction slot " + std::to_string(i) + " (" + slotInfo->slotName + ") - downstream node will handle per-frame extraction");
            continue;
        }

        VkDescriptorType expectedType = slotInfo->descriptorType;
        if (!ValidateResourceType(res, expectedType)) {
            NODE_LOG_INFO("[DescriptorResourceGathererNode::ValidateVariadicInputsImpl] ERROR: Resource " + std::to_string(i) + " (" + slotInfo->slotName + ") type mismatch for shader binding " + std::to_string(slotInfo->binding) + " (expected VkDescriptorType=" + std::to_string(expectedType) + ")");
            allValid = false;
        }
    }

    return allValid;
}

void DescriptorResourceGathererNode::GatherResources(VariadicCompileContext& ctx) {
    // Gather validated variadic slots into resource array, indexed by binding
    // All bundle access goes through context - taskIndex handled automatically
    size_t inputCount = ctx.InVariadicCount();

    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::GatherResources] Gathering " + std::to_string(inputCount) + " validated slots");

    for (size_t i = 0; i < inputCount; ++i) {
        const auto* slotInfo = ctx.InVariadicSlot(i);
        if (!slotInfo) {
            NODE_LOG_DEBUG("[DescriptorResourceGathererNode::GatherResources] WARNING: Null slot at index " + std::to_string(i));
            continue;
        }

        // Skip invalid slots (failed validation)
        if (slotInfo->state == SlotState::Invalid) {
            NODE_LOG_DEBUG("[DescriptorResourceGathererNode::GatherResources] Skipping invalid slot " + std::to_string(i) + " (binding=" + std::to_string(slotInfo->binding) + ")");
            continue;
        }

        uint32_t binding = slotInfo->binding;

        // Store slot role even for Execute slots (needed for filtering in DescriptorSetNode)
        slotRoleArray_[binding] = slotInfo->slotRole;

        // For Execute-ONLY slots (no Dependency flag), skip resource gathering in Compile
        // Slots with Dependency flag (including Dependency|Execute) need initial gather here
        if (!HasDependency(slotInfo->slotRole)) {
            // Initialize placeholder entry to prevent accessing uninitialized memory
            // This ensures resourceArray_[binding] exists even before Execute phase
            resourceArray_[binding] = std::monostate{};
            NODE_LOG_DEBUG("[DescriptorResourceGathererNode::GatherResources] Recorded role for Execute-only slot " + std::to_string(i) + " (binding=" + std::to_string(binding) + ", role=" + std::to_string(static_cast<uint8_t>(slotInfo->slotRole)) + ") - placeholder initialized, resource will be gathered in Execute phase");
            continue;
        }

        // Slot should have valid resource after validation (for non-transient)
        if (!slotInfo->resource) {
            NODE_LOG_DEBUG("[DescriptorResourceGathererNode::GatherResources] WARNING: Validated slot " + std::to_string(i) + " (binding=" + std::to_string(binding) + ") has null resource");
            continue;
        }

        auto variant = slotInfo->resource->GetHandleVariant();

        NODE_LOG_DEBUG("[DescriptorResourceGathererNode::GatherResources] Slot " + std::to_string(i) + " variant index=" + std::to_string(variant.index()) + ", resource type=" + std::to_string(static_cast<int>(slotInfo->resource->GetType())) + ", isValid=" + std::to_string(slotInfo->resource->IsValid()) + ", hasFieldExtraction=" + std::to_string(slotInfo->hasFieldExtraction));

        // Handle field extraction from struct using runtime type dispatch
        if (slotInfo->hasFieldExtraction && slotInfo->fieldOffset != 0) {
            NODE_LOG_DEBUG("[DescriptorResourceGathererNode::GatherResources] Extracting field at offset " + std::to_string(slotInfo->fieldOffset) + " from struct for binding " + std::to_string(binding));

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
                    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::GatherResources] WARNING: Cannot extract raw pointer from variant type");
                    return;
                }

                if (!rawStructPtr) {
                    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::GatherResources] WARNING: Null struct pointer");
                    return;
                }

                // Apply field offset to get field pointer
                auto* fieldPtr = reinterpret_cast<const char*>(rawStructPtr) + slotInfo->fieldOffset;

                // Extract field value based on resourceType - dispatch to correct type
                // NOTE: This requires knowing the actual field type at runtime
                // For now, we store the struct and let downstream nodes handle extraction
                resourceArray_[binding] = variant;  // Store original struct

                NODE_LOG_DEBUG("[DescriptorResourceGathererNode::GatherResources] Stored struct with field at offset " + std::to_string(slotInfo->fieldOffset) + " for binding " + std::to_string(binding) + " (downstream will extract)");
            }, variant);
        }
        // Regular resources - store directly at binding index
        else {
            resourceArray_[binding] = variant;
            // Note: slotRoleArray_[binding] already set earlier in this function
            NODE_LOG_DEBUG("[DescriptorResourceGathererNode::GatherResources] Gathered resource for binding " + std::to_string(binding) + " (" + slotInfo->slotName + "), variant index=" + std::to_string(variant.index()) + ", role=" + std::to_string(static_cast<int>(slotInfo->slotRole)));
        }
    }

    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::GatherResources] Gathered " + std::to_string(resourceArray_.size()) + " total resources");
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
        NODE_LOG_DEBUG("[DescriptorResourceGathererNode::IsResourceCompatibleWithDescriptorType] Found BufferDescriptor with usage=" + std::to_string(static_cast<uint32_t>(bufferDesc->usage)));
    }
    // Visit ImageDescriptor
    else if (auto* imageDesc = res->GetDescriptor<ImageDescriptor>()) {
        usageOpt = imageDesc->usage;
        NODE_LOG_DEBUG("[DescriptorResourceGathererNode::IsResourceCompatibleWithDescriptorType] Found ImageDescriptor with usage=" + std::to_string(static_cast<uint32_t>(imageDesc->usage)));
    }
    // Visit StorageImageDescriptor
    else if (auto* storageImageDesc = res->GetDescriptor<StorageImageDescriptor>()) {
        usageOpt = ResourceUsage::Storage;  // Storage images always have Storage usage
        NODE_LOG_DEBUG("[DescriptorResourceGathererNode::IsResourceCompatibleWithDescriptorType] Found StorageImageDescriptor");
    }
    // Visit Texture3DDescriptor
    else if (auto* texture3DDesc = res->GetDescriptor<Texture3DDescriptor>()) {
        usageOpt = ResourceUsage::Sampled;  // 3D textures typically sampled
        NODE_LOG_DEBUG("[DescriptorResourceGathererNode::IsResourceCompatibleWithDescriptorType] Found Texture3DDescriptor");
    }
    else {
        NODE_LOG_DEBUG("[DescriptorResourceGathererNode::IsResourceCompatibleWithDescriptorType] No descriptor with usage found, using fallback");
    }

    // If no usage available, fall back to ResourceType-based compatibility
    if (!usageOpt.has_value()) {
        bool fallbackResult = IsResourceTypeCompatibleWithDescriptor(resType, descriptorType);
        NODE_LOG_DEBUG("[DescriptorResourceGathererNode::IsResourceCompatibleWithDescriptorType] Fallback result for ResourceType=" + std::to_string(static_cast<int>(resType)) + ", VkDescriptorType=" + std::to_string(descriptorType) + ": " + (fallbackResult ? "PASS" : "FAIL"));
        return fallbackResult;
    }

    ResourceUsage usage = usageOpt.value();
    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::IsResourceCompatibleWithDescriptorType] Checking usage=" + std::to_string(static_cast<uint32_t>(usage)) + " against VkDescriptorType=" + std::to_string(descriptorType));

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
