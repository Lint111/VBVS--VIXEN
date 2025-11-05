#include "Data/Core/ResourceVariant.h"

namespace Vixen::RenderGraph {

/**
 * @brief Create resource from ResourceType enum (legacy runtime dispatch)
 * 
 * Uses macro-generated InitializeResourceFromType() - automatically uses
 * correct variant types from RESOURCE_TYPE_REGISTRY!
 */
Resource Resource::CreateFromType(ResourceType type, std::unique_ptr<ResourceDescriptorBase> desc) {
    Resource res;
    res.type = type;
    res.lifetime = ResourceLifetime::Transient;

    // Use macro-generated dispatch (defined in ResourceVariant.h)
    if (!InitializeResourceFromType(type, desc.get(), res.handle, res.descriptor)) {
        // Fallback for unknown types
        res.descriptor = HandleDescriptor("UnknownType");
        res.handle = std::monostate{};
    }

    return res;
}

} // namespace Vixen::RenderGraph
