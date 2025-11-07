// #include "Nodes/StructSpreaderNode.h"
// #include "VulkanSwapChain.h"
// #include <iostream>

// namespace Vixen::RenderGraph {

// //-----------------------------------------------------------------------------
// // StructSpreaderNodeType Implementation
// //-----------------------------------------------------------------------------

// std::unique_ptr<NodeInstance> StructSpreaderNodeType::CreateInstance(
//     const std::string& instanceName
// ) const {
//     return std::make_unique<StructSpreaderNode>(instanceName, const_cast<StructSpreaderNodeType*>(this));
// }

// //-----------------------------------------------------------------------------
// // StructSpreaderNode Implementation
// //-----------------------------------------------------------------------------

// StructSpreaderNode::StructSpreaderNode(
//     const std::string& instanceName,
//     NodeType* nodeType
// ) : VariadicTypedNode<StructSpreaderNodeConfig>(instanceName, nodeType) {
// }

// void StructSpreaderNode::SetupImpl(Context& ctx) {
//     std::cout << "[StructSpreaderNode::Setup] Initializing struct spreader\n";

//     // Get struct resource from input
//     auto structPtr = ctx.In(StructSpreaderNodeConfig::STRUCT_RESOURCE);
//     if (!structPtr) {
//         std::cout << "[StructSpreaderNode::Setup] ERROR: No struct resource input\n";
//         return;
//     }

//     structPtr_ = structPtr;

//     // Validate it's a struct with multiple members
//     if (memberMetadata_.empty()) {
//         std::cout << "[StructSpreaderNode::Setup] ERROR: No struct members registered. "
//                   << "Call PreRegisterMembers() before graph compilation.\n";
//         return;
//     }

//     if (memberMetadata_.size() < 2) {
//         std::cout << "[StructSpreaderNode::Setup] WARNING: Struct has only " << memberMetadata_.size()
//                   << " member. Spreading may not be necessary.\n";
//     }

//     std::cout << "[StructSpreaderNode::Setup] Struct pointer: " << structPtr_
//               << ", registered members: " << memberMetadata_.size() << "\n";
// }

// void StructSpreaderNode::CompileImpl(Context& ctx) {
//     std::cout << "[StructSpreaderNode::Compile] Spreading struct members into variadic outputs\n";

//     if (!structPtr_) {
//         std::cout << "[StructSpreaderNode::Compile] ERROR: Null struct pointer\n";
//         return;
//     }

//     // Cast to SwapChainPublicVariables* to access members
//     auto* swapChainVars = static_cast<SwapChainPublicVariables*>(structPtr_);

//     std::cout << "[StructSpreaderNode::Compile] SwapChainPublicVariables at " << swapChainVars
//               << ", imageCount=" << swapChainVars->swapChainImageCount << "\n";

//     // For each registered member, create a resource and output it
//     for (size_t i = 0; i < memberMetadata_.size(); ++i) {
//         const auto& member = memberMetadata_[i];

//         // Calculate pointer to member within struct
//         void* memberPtr = reinterpret_cast<char*>(swapChainVars) + member.offset;

//         std::cout << "[StructSpreaderNode::Compile] Member " << i << " (" << member.name
//                   << ") at offset " << member.offset << ", ptr=" << memberPtr << "\n";

//         // Create resource pointing to the member
//         // For std::vector<VkImageView>*, we store pointer to the vector
//         Resource memberResource = Resource::Create(
//             member.resourceType,
//             HandleDescriptor{member.name}
//         );

//         memberResource.SetHandle(memberPtr);

//         // Output the member resource
//         ctx.Out(i, memberResource);

//         std::cout << "[StructSpreaderNode::Compile] Output member " << i << " as variadic output slot\n";
//     }

//     std::cout << "[StructSpreaderNode::Compile] Spread " << memberMetadata_.size() << " members to outputs\n";
// }

// void StructSpreaderNode::ExecuteImpl(Context& ctx) {
//     // No per-frame work - struct spreading happens during Compile
// }

// void StructSpreaderNode::CleanupImpl(Context& ctx) {
//     structPtr_ = nullptr;
// }

// } // namespace Vixen::RenderGraph
