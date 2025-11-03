#include "Nodes/DescriptorResourceGathererNode.h"
#include "ShaderManagement/ShaderDataBundle.h"
#include "VulkanSwapChain.h"  // For SwapChainPublicVariables
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

void DescriptorResourceGathererNode::GraphCompileSetup() {
    std::cout << "[DescriptorResourceGathererNode::GraphCompileSetup] Discovering descriptors from shader bundle...\n";

    // Check if slots were already pre-registered
    if (!descriptorSlots_.empty()) {
        std::cout << "[DescriptorResourceGathererNode::GraphCompileSetup] Slots already pre-registered ("
                  << descriptorSlots_.size() << " bindings), skipping auto-discovery\n";
        return;
    }

    // Get shader bundle from input (bundle 0, slot 0)
    Resource* shaderBundleResource = GetInput(DescriptorResourceGathererNodeConfig::SHADER_DATA_BUNDLE.index, 0);
    if (!shaderBundleResource || !shaderBundleResource->IsValid()) {
        std::cout << "[DescriptorResourceGathererNode::GraphCompileSetup] WARNING: No shader bundle connected, cannot auto-discover descriptors\n";
        return;
    }

    // Extract ShaderDataBundle from resource
    auto shaderBundle = shaderBundleResource->GetHandle<ShaderDataBundle*>();
    if (!shaderBundle || !shaderBundle->descriptorLayout) {
        std::cout << "[DescriptorResourceGathererNode::GraphCompileSetup] WARNING: Invalid shader bundle or no descriptor layout\n";
        return;
    }

    const auto* layoutSpec = shaderBundle->descriptorLayout.get();
    std::cout << "[DescriptorResourceGathererNode::GraphCompileSetup] Found " << layoutSpec->bindings.size()
              << " descriptor bindings in shader\n";

    // Register variadic slots based on shader reflection
    for (const DescriptorBinding& binding : layoutSpec->bindings) {
        // Create descriptor slot info
        DescriptorSlotInfo slotInfo;
        slotInfo.binding = binding.binding;
        slotInfo.descriptorType = binding.descriptorType;
        slotInfo.dynamicInputIndex = descriptorSlots_.size();

        // Generate slot name
        switch (binding.descriptorType) {
            case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                slotInfo.slotName = "storage_image_" + std::to_string(binding.binding);
                break;
            case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
                slotInfo.slotName = "sampled_image_" + std::to_string(binding.binding);
                break;
            case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                slotInfo.slotName = "combined_sampler_" + std::to_string(binding.binding);
                break;
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                slotInfo.slotName = "uniform_buffer_" + std::to_string(binding.binding);
                break;
            case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                slotInfo.slotName = "storage_buffer_" + std::to_string(binding.binding);
                break;
            default:
                slotInfo.slotName = "descriptor_" + std::to_string(binding.binding);
                break;
        }

        descriptorSlots_.push_back(slotInfo);

        // Register variadic slot with bundle system
        VariadicSlotInfo variadicSlot;
        variadicSlot.resource = nullptr;
        variadicSlot.resourceType = ResourceType::Image;  // Default
        variadicSlot.slotName = slotInfo.slotName;
        variadicSlot.binding = binding.binding;
        variadicSlot.descriptorType = binding.descriptorType;

        RegisterVariadicSlot(variadicSlot, 0);

        std::cout << "[DescriptorResourceGathererNode::GraphCompileSetup] Auto-registered variadic slot "
                  << binding.binding << ": " << variadicSlot.slotName
                  << " (type=" << binding.descriptorType << ")\n";
    }

    // Set variadic input constraints
    if (!descriptorSlots_.empty()) {
        SetVariadicInputConstraints(descriptorSlots_.size(), descriptorSlots_.size());
        std::cout << "[DescriptorResourceGathererNode::GraphCompileSetup] Set exact count constraint: "
                  << descriptorSlots_.size() << " required\n";
    }
}

void DescriptorResourceGathererNode::SetupImpl(Context& ctx) {
    std::cout << "[DescriptorResourceGathererNode::Setup] Discovering descriptor requirements...\n";

    // Check if slots were pre-registered during graph construction
    bool slotsPreRegistered = !descriptorSlots_.empty();

    if (slotsPreRegistered) {
        std::cout << "[DescriptorResourceGathererNode::Setup] Using pre-registered variadic slots ("
                  << descriptorSlots_.size() << " bindings)\n";
        // Slots already registered, just validate against shader bundle during Compile
        return;
    }

    // Discover descriptor metadata from shader (fallback if not pre-registered)
    DiscoverDescriptors(ctx);

    // Set variadic input constraints based on shader requirements
    size_t requiredCount = descriptorSlots_.size();
    SetVariadicInputConstraints(requiredCount, requiredCount);  // Exact match required

    std::cout << "[DescriptorResourceGathererNode::Setup] Discovered " << descriptorSlots_.size()
              << " descriptor bindings from shader (exact count required)\n";
}

void DescriptorResourceGathererNode::CompileImpl(Context& ctx) {
    std::cout << "[DescriptorResourceGathererNode::Compile] Validating variadic inputs...\n";

    // Call virtual validation hook (shader-specific validation)
    if (!ValidateVariadicInputsImpl(ctx)) {
        std::cout << "[DescriptorResourceGathererNode::Compile] ERROR: Variadic input validation failed\n";
        return;
    }

    // Resize output array to accommodate all bindings
    if (!descriptorSlots_.empty()) {
        uint32_t maxBinding = 0;
        for (const auto& slot : descriptorSlots_) {
            maxBinding = std::max(maxBinding, slot.binding);
        }
        resourceArray_.resize(maxBinding + 1);
    }

    std::cout << "[DescriptorResourceGathererNode::Compile] Validation complete. Ready to gather "
              << GetVariadicInputCount() << " resources\n";

    // Gather resources at compile-time (descriptor sets need resources during Compile phase)
    GatherResources(ctx);

    // Output the resource array for downstream nodes
    ctx.Out(DescriptorResourceGathererNodeConfig::DESCRIPTOR_RESOURCES, resourceArray_);

    // Pass through shader bundle for downstream nodes
    ctx.Out(DescriptorResourceGathererNodeConfig::SHADER_DATA_BUNDLE_OUT, ctx.In(DescriptorResourceGathererNodeConfig::SHADER_DATA_BUNDLE));

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

void DescriptorResourceGathererNode::DiscoverDescriptors(Context& ctx) {
    // Get shader data bundle from input
    auto shaderBundle = ctx.In(DescriptorResourceGathererNodeConfig::SHADER_DATA_BUNDLE);
    if (!shaderBundle) {
        std::cout << "[DescriptorResourceGathererNode::DiscoverDescriptors] ERROR: No shader bundle provided\n";
        return;
    }

    // Get descriptor layout spec from shader bundle
    const auto* layoutSpec = shaderBundle->descriptorLayout.get();
    if (!layoutSpec) {
        std::cout << "[DescriptorResourceGathererNode::DiscoverDescriptors] ERROR: Shader bundle has no descriptor layout spec\n";
        return;
    }

    // Clear old descriptor slots
    descriptorSlots_.clear();

    // Register variadic slots based on shader reflection
    for (const auto& binding : layoutSpec->bindings) {
        // Create descriptor slot info struct
        DescriptorSlotInfo legacySlotInfo;
        legacySlotInfo.binding = binding.binding;
        legacySlotInfo.descriptorType = binding.descriptorType;
        legacySlotInfo.dynamicInputIndex = descriptorSlots_.size();

        // Generate slot name based on descriptor type and binding
        switch (binding.descriptorType) {
            case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                legacySlotInfo.slotName = "storage_image_" + std::to_string(binding.binding);
                break;
            case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
                legacySlotInfo.slotName = "sampled_image_" + std::to_string(binding.binding);
                break;
            case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                legacySlotInfo.slotName = "combined_sampler_" + std::to_string(binding.binding);
                break;
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                legacySlotInfo.slotName = "uniform_buffer_" + std::to_string(binding.binding);
                break;
            case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                legacySlotInfo.slotName = "storage_buffer_" + std::to_string(binding.binding);
                break;
            default:
                legacySlotInfo.slotName = "descriptor_" + std::to_string(binding.binding);
                break;
        }

        descriptorSlots_.push_back(legacySlotInfo);

        // Create variadic slot info for bundle system
        VariadicSlotInfo variadicSlot;
        variadicSlot.resource = nullptr;  // Will be connected later
        variadicSlot.resourceType = ResourceType::Image;  // Default - will be validated at connection time
        variadicSlot.slotName = legacySlotInfo.slotName;
        variadicSlot.binding = binding.binding;
        variadicSlot.descriptorType = binding.descriptorType;

        // Register variadic slot with bundle 0 (default bundle)
        RegisterVariadicSlot(variadicSlot, 0);

        std::cout << "[DescriptorResourceGathererNode::DiscoverDescriptors] Registered variadic slot "
                  << binding.binding << ": " << variadicSlot.slotName
                  << " (type=" << binding.descriptorType << ")\n";
    }
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
    // Gather variadic inputs from bundle 0 into resource array, indexed by binding
    size_t bundleIndex = 0;
    size_t inputCount = GetVariadicInputCount(bundleIndex);

    for (size_t i = 0; i < inputCount; ++i) {
        // Get variadic slot info (includes resource and metadata)
        const auto* slotInfo = GetVariadicSlotInfo(i, bundleIndex);
        if (!slotInfo || !slotInfo->resource) {
            std::cout << "[DescriptorResourceGathererNode::GatherResources] Skipping null resource at variadic index " << i << "\n";
            continue;
        }

        uint32_t binding = slotInfo->binding;

        // Extract handle variant and store in output array
        auto variant = slotInfo->resource->GetHandleVariant();

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
            std::cout << "[DescriptorResourceGathererNode::GatherResources] Skipping invalid resource at variadic index " << i << "\n";
        }
    }
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
