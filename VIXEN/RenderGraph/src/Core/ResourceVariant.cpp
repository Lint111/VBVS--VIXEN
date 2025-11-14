// #include "Data/Core/ResourceV3.h"

// namespace Vixen::RenderGraph {

// /**
//  * @brief Create resource from ResourceType enum (legacy runtime dispatch)
//  * 
//  * Uses macro-generated InitializeResourceFromType() - automatically uses
//  * correct variant types from RESOURCE_TYPE_REGISTRY!
//  */
// Resource Resource::CreateFromType(ResourceType type, std::unique_ptr<ResourceDescriptorBase> desc) {
//     Resource res;
//     res.type = type;
//     res.lifetime = ResourceLifetime::Transient;

//     // Initialize handleStorage with a ResourceVariant for Case 1
//     ResourceVariant tempVariant;

//     // Use macro-generated dispatch (defined in ResourceVariant.h)
//     if (!InitializeResourceFromType(type, desc.get(), tempVariant, res.descriptor)) {
//         // Fallback for unknown types
//         res.descriptor = HandleDescriptor("UnknownType");
//         res.handleStorage = std::monostate{};
//     } else {
//         // Store the initialized variant in handleStorage (Case 1)
//         // Use in-place construction to avoid invoking deleted copy-assignment
//         // overloads on std::variant when some alternatives are non-copyable.
//         res.handleStorage.template emplace<ResourceVariant>(std::move(tempVariant));
//     }

//     return res;
// }

// } // namespace Vixen::RenderGraph
