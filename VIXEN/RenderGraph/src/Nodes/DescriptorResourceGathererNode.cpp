#include "Nodes/DescriptorResourceGathererNode.h"
#include "ShaderManagement/ShaderDataBundle.h"
#include "ShaderManagement/SpirvReflectionData.h"  // For DescriptorLayoutSpecification and DescriptorBindingInfo
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

    // Find max binding to size output arrays
    uint32_t maxBinding = 0;
    for (const auto& binding : layoutSpec->bindings) {
        maxBinding = std::max(maxBinding, binding.binding);
    }
    resourceArray_.resize(maxBinding + 1);
    slotRoleArray_.resize(maxBinding + 1, SlotRole::Dependency);  // Default to Dependency

    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::Compile] Validation complete. Gathering " + std::to_string(GetVariadicInputCount()) + " resources");

    // Gather resources from validated slots
    GatherResources(ctx);

    // Debug: Log slot roles being output
    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::Compile] Outputting slot roles:");
    for (size_t i = 0; i < slotRoleArray_.size(); ++i) {
        uint8_t roleVal = static_cast<uint8_t>(slotRoleArray_[i]);
        NODE_LOG_DEBUG("  Binding " + std::to_string(i) + ": role=" + std::to_string(roleVal));
    }

    // Output resource array, slot roles, and pass through shader bundle
    ctx.Out(DescriptorResourceGathererNodeConfig::DESCRIPTOR_RESOURCES, resourceArray_);
    ctx.Out(DescriptorResourceGathererNodeConfig::DESCRIPTOR_SLOT_ROLES, slotRoleArray_);
    ctx.Out(DescriptorResourceGathererNodeConfig::SHADER_DATA_BUNDLE_OUT, shaderBundle);

    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::Compile] Output DESCRIPTOR_RESOURCES with " + std::to_string(resourceArray_.size()) + " entries");
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
        // Re-output updated resource array
        ctx.Out(DescriptorResourceGathererNodeConfig::DESCRIPTOR_RESOURCES, resourceArray_);
        NODE_LOG_DEBUG("[DescriptorResourceGathererNode::Execute] Re-output DESCRIPTOR_RESOURCES with " + std::to_string(resourceArray_.size()) + " entries (transients updated)");
    }
}

void DescriptorResourceGathererNode::CleanupImpl(VariadicCleanupContext& ctx) {
    descriptorSlots_.clear();
    resourceArray_.clear();
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

    size_t variadicCount = ctx.InVariadicCount();
    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::ValidateTentativeSlots] Validating " + std::to_string(variadicCount) + " tentative slots against " + std::to_string(layoutSpec->bindings.size()) + " shader bindings");

    if (!GetOwningGraph()) {
        NODE_LOG_INFO("[DescriptorResourceGathererNode::ValidateTentativeSlots] ERROR: No owning graph");
        return;
    }

    // Validate and update all tentative slots against shader requirements
    for (size_t i = 0; i < variadicCount; ++i) {
        const auto* slotInfo = ctx.InVariadicSlot(i);
        if (!slotInfo || slotInfo->state != SlotState::Tentative) {
            continue;  // Skip non-tentative slots
        }

        ValidateSingleSlotAgainstShader(ctx, i, slotInfo, layoutSpec);
    }
}

void DescriptorResourceGathererNode::ValidateSingleSlotAgainstShader(
    VariadicCompileContext& ctx,
    size_t slotIndex,
    const VariadicSlotInfo* slotInfo,
    const ShaderManagement::DescriptorLayoutSpec* layoutSpec
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
    const ShaderManagement::DescriptorBindingSpec& shaderBinding
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

    for (size_t i = 0; i < inputCount; ++i) {
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

    // Skip validation for transient slots (Execute) - validated in Execute phase
    if (ShouldSkipTransientSlot(slotInfo, slotIndex)) {
        return true;
    }

    // Skip type validation for field extraction - DescriptorSetNode handles per-frame indexing
    if (ShouldSkipFieldExtractionSlot(slotInfo, slotIndex)) {
        return true;
    }

    // Validate resource type against expected descriptor type
    Resource* res = ctx.InVariadicResource(slotIndex);
    VkDescriptorType expectedType = slotInfo->descriptorType;

    if (!ValidateResourceType(res, expectedType)) {
        LogTypeValidationError(slotIndex, slotInfo, expectedType);
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

    // Skip invalid slots (failed validation)
    if (slotInfo->state == SlotState::Invalid) {
        NODE_LOG_DEBUG("[DescriptorResourceGathererNode::ProcessSlot] Skipping invalid slot " + std::to_string(slotIndex) + " (binding=" + std::to_string(slotInfo->binding) + ")");
        return false;
    }

    uint32_t binding = slotInfo->binding;
    slotRoleArray_[binding] = slotInfo->slotRole;

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

    auto variant = slotInfo->resource->GetHandleVariant();
    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::ProcessSlot] Slot " + std::to_string(slotIndex) + " variant index=" + std::to_string(variant.index()) + ", resource type=" + std::to_string(static_cast<int>(slotInfo->resource->GetType())) + ", isValid=" + std::to_string(slotInfo->resource->IsValid()) + ", hasFieldExtraction=" + std::to_string(slotInfo->hasFieldExtraction));

    // Store resource (field extraction or regular)
    if (slotInfo->hasFieldExtraction && slotInfo->fieldOffset != 0) {
        StoreFieldExtractionResource(slotIndex, binding, slotInfo->fieldOffset, variant);
    } else {
        StoreRegularResource(slotIndex, binding, slotInfo->slotName, slotInfo->slotRole, variant);
    }

    return true;
}

void DescriptorResourceGathererNode::InitializeExecuteOnlySlot(size_t slotIndex, uint32_t binding, SlotRole role) {
    // Initialize placeholder entry to prevent accessing uninitialized memory
    // This ensures resourceArray_[binding] exists even before Execute phase
    resourceArray_[binding] = std::monostate{};
    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::InitializeExecuteOnlySlot] Recorded role for Execute-only slot " + std::to_string(slotIndex) + " (binding=" + std::to_string(binding) + ", role=" + std::to_string(static_cast<uint8_t>(role)) + ") - placeholder initialized, resource will be gathered in Execute phase");
}

void DescriptorResourceGathererNode::StoreFieldExtractionResource(size_t slotIndex, uint32_t binding, size_t fieldOffset, const ResourceVariant& variant) {
    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::StoreFieldExtractionResource] Extracting field at offset " + std::to_string(fieldOffset) + " from struct for binding " + std::to_string(binding));

    // Extract field using visitor pattern - get raw struct pointer from variant
    std::visit([&](auto&& structPtr) {
        const void* rawStructPtr = ExtractRawPointerFromVariant(structPtr);
        if (!rawStructPtr) {
            return;
        }

        // Apply field offset to get field pointer (not currently used - stored for downstream)
        auto* fieldPtr = reinterpret_cast<const char*>(rawStructPtr) + fieldOffset;

        // Store original struct - downstream nodes will handle extraction
        resourceArray_[binding] = variant;

        NODE_LOG_DEBUG("[DescriptorResourceGathererNode::StoreFieldExtractionResource] Stored struct with field at offset " + std::to_string(fieldOffset) + " for binding " + std::to_string(binding) + " (downstream will extract)");
    }, variant);
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

void DescriptorResourceGathererNode::StoreRegularResource(size_t slotIndex, uint32_t binding, const std::string& slotName, SlotRole role, const ResourceVariant& variant) {
    resourceArray_[binding] = variant;
    NODE_LOG_DEBUG("[DescriptorResourceGathererNode::StoreRegularResource] Gathered resource for binding " + std::to_string(binding) + " (" + slotName + "), variant index=" + std::to_string(variant.index()) + ", role=" + std::to_string(static_cast<int>(role)));
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
