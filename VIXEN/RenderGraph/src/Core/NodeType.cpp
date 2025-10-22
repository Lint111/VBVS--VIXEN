#include "RenderGraph/NodeType.h"
#include "RenderGraph/NodeInstance.h"

namespace Vixen::RenderGraph {

bool NodeType::ValidateInputs(const std::vector<Resource*>& inputs) const {
    if (inputs.size() != inputSchema.size()) {
        // Check if any missing inputs are optional
        size_t requiredCount = 0;
        for (const auto& desc : inputSchema) {
            if (!desc.optional) {
                requiredCount++;
            }
        }
        
        if (inputs.size() < requiredCount) {
            return false;
        }
    }

    // Validate each input matches its schema
    for (size_t i = 0; i < inputs.size() && i < inputSchema.size(); ++i) {
        if (!inputs[i]) continue; // null inputs allowed for optional

        const auto& schema = inputSchema[i];
        const auto* resource = inputs[i];

        // Type must match
        if (resource->GetType() != schema.type) {
            return false;
        }

        // Validate type-specific properties
        if (schema.type == ResourceType::Image ||
            schema.type == ResourceType::CubeMap ||
            schema.type == ResourceType::Image3D ||
            schema.type == ResourceType::StorageImage) {

            const auto* schemaDesc = dynamic_cast<const ImageDescription*>(schema.description.get());
            const auto* resourceDesc = resource->GetImageDescription();

            if (!schemaDesc || !resourceDesc) {
                return false;
            }

            // Check format compatibility (could be relaxed with format conversion)
            if (schemaDesc->format != VK_FORMAT_UNDEFINED &&
                resourceDesc->format != schemaDesc->format) {
                return false;
            }
        }
    }

    return true;
}

bool NodeType::ValidateOutputs(const std::vector<Resource*>& outputs) const {
    if (outputs.size() != outputSchema.size()) {
        return false;
    }

    // Output validation similar to inputs
    for (size_t i = 0; i < outputs.size(); ++i) {
        if (!outputs[i]) {
            return false; // All outputs must be present
        }

        const auto& schema = outputSchema[i];
        const auto* resource = outputs[i];

        if (resource->GetType() != schema.type) {
            return false;
        }
    }

    return true;
}

} // namespace Vixen::RenderGraph
