#include "Nodes/DescriptorResourceGathererNode.h"
#include "ShaderManagement/ShaderDataBundle.h"
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

void DescriptorResourceGathererNode::SetupImpl(Context& ctx) {
    std::cout << "[DescriptorResourceGathererNode::Setup] Discovering descriptor requirements...\n";

    // Discover descriptor metadata from shader
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
}

void DescriptorResourceGathererNode::ExecuteImpl(Context& ctx) {
    // Gather resources from dynamic inputs
    GatherResources(ctx);

    // Output the resource array
    OUTPUT(DESCRIPTOR_RESOURCES) = resourceArray_;

    // Pass through shader bundle for downstream nodes
    OUTPUT(SHADER_DATA_BUNDLE_OUT) = INPUT(SHADER_DATA_BUNDLE);
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
    auto shaderBundle = INPUT(SHADER_DATA_BUNDLE);
    if (!shaderBundle) {
        std::cout << "[DescriptorResourceGathererNode::DiscoverDescriptors] ERROR: No shader bundle provided\n";
        return;
    }

    // Get descriptor layout spec from shader bundle
    const auto* layoutSpec = shaderBundle->descriptorLayoutSpec;
    if (!layoutSpec) {
        std::cout << "[DescriptorResourceGathererNode::DiscoverDescriptors] ERROR: Shader bundle has no descriptor layout spec\n";
        return;
    }

    // Create slot info for each binding
    descriptorSlots_.clear();
    for (const auto& binding : layoutSpec->bindings) {
        DescriptorSlotInfo slotInfo;
        slotInfo.binding = binding.binding;
        slotInfo.descriptorType = binding.descriptorType;

        // Generate slot name based on descriptor type and binding
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

        slotInfo.dynamicInputIndex = descriptorSlots_.size();
        descriptorSlots_.push_back(slotInfo);

        std::cout << "[DescriptorResourceGathererNode::DiscoverDescriptors] Discovered binding "
                  << binding.binding << ": " << slotInfo.slotName
                  << " (type=" << binding.descriptorType << ")\n";
    }
}

bool DescriptorResourceGathererNode::ValidateVariadicInputsImpl(Context& ctx) {
    // Call base class validation (count constraints, null checks)
    if (!VariadicTypedNode<DescriptorResourceGathererNodeConfig>::ValidateVariadicInputsImpl(ctx)) {
        return false;  // Base validation failed
    }

    size_t inputCount = GetVariadicInputCount();

    // Shader-specific validation: type matching
    bool allValid = true;
    for (size_t i = 0; i < inputCount; ++i) {
        Resource* res = GetVariadicInputResource(i);

        VkDescriptorType expectedType = descriptorSlots_[i].descriptorType;
        if (!ValidateResourceType(res, expectedType)) {
            std::cout << "[DescriptorResourceGathererNode::ValidateVariadicInputsImpl] ERROR: Resource " << i
                      << " type mismatch for shader binding " << descriptorSlots_[i].binding
                      << " (expected VkDescriptorType=" << expectedType << ")\n";
            allValid = false;
        }
    }

    return allValid;
}

void DescriptorResourceGathererNode::GatherResources(Context& ctx) {
    // Gather variadic inputs into resource array, indexed by binding
    size_t inputCount = GetVariadicInputCount();

    for (size_t i = 0; i < std::min(inputCount, descriptorSlots_.size()); ++i) {
        Resource* res = GetVariadicInputResource(i);
        if (!res || !res->IsValid()) {
            std::cout << "[DescriptorResourceGathererNode::GatherResources] Skipping invalid resource at index " << i << "\n";
            continue;
        }

        uint32_t binding = descriptorSlots_[i].binding;

        // TODO: Need Resource::GetHandleVariant() to extract variant properly
        // For now, placeholder - we'll need to extend Resource class
        std::cout << "[DescriptorResourceGathererNode::GatherResources] Gathered resource for binding " << binding << "\n";
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
