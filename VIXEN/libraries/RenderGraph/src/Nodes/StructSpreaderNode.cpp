// #include "Nodes/StructSpreaderNode.h"
// #include "VulkanResources/VulkanSwapChain.h"
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
//     NODE_LOG_DEBUG("[StructSpreaderNode::Setup] Initializing struct spreader");

//     // Get struct resource from input
//     auto structPtr = ctx.In(StructSpreaderNodeConfig::STRUCT_RESOURCE);
//     if (!structPtr) {
//         NODE_LOG_ERROR("[StructSpreaderNode::Setup] ERROR: No struct resource input");
//         return;
//     }

//     structPtr_ = structPtr;

//     // Validate it's a struct with multiple members
//     if (memberMetadata_.empty()) {
//         NODE_LOG_ERROR("[StructSpreaderNode::Setup] ERROR: No struct members registered. "
//                   "Call PreRegisterMembers() before graph compilation.");
//         return;
//     }

//     if (memberMetadata_.size() < 2) {
//         NODE_LOG_WARNING("[StructSpreaderNode::Setup] WARNING: Struct has only " + std::to_string(memberMetadata_.size())
//                   + " member. Spreading may not be necessary.");
//     }

//     NODE_LOG_DEBUG("[StructSpreaderNode::Setup] Struct pointer: " + std::to_string(reinterpret_cast<uintptr_t>(structPtr_))
//               + ", registered members: " + std::to_string(memberMetadata_.size()));
// }

// void StructSpreaderNode::CompileImpl(Context& ctx) {
//     NODE_LOG_DEBUG("[StructSpreaderNode::Compile] Spreading struct members into variadic outputs");

//     if (!structPtr_) {
//         NODE_LOG_ERROR("[StructSpreaderNode::Compile] ERROR: Null struct pointer");
//         return;
//     }

//     // Cast to SwapChainPublicVariables* to access members
//     auto* swapChainVars = static_cast<SwapChainPublicVariables*>(structPtr_);

//     NODE_LOG_DEBUG("[StructSpreaderNode::Compile] SwapChainPublicVariables at " + std::to_string(reinterpret_cast<uintptr_t>(swapChainVars))
//               + ", imageCount=" + std::to_string(swapChainVars->swapChainImageCount));

//     // For each registered member, create a resource and output it
//     for (size_t i = 0; i < memberMetadata_.size(); ++i) {
//         const auto& member = memberMetadata_[i];

//         // Calculate pointer to member within struct
//         void* memberPtr = reinterpret_cast<char*>(swapChainVars) + member.offset;

//         NODE_LOG_DEBUG("[StructSpreaderNode::Compile] Member " + std::to_string(i) + " (" + member.name
//                   + ") at offset " + std::to_string(member.offset) + ", ptr=" + std::to_string(reinterpret_cast<uintptr_t>(memberPtr)));

//         // Create resource pointing to the member
//         // For std::vector<VkImageView>*, we store pointer to the vector
//         Resource memberResource = Resource::Create(
//             member.resourceType,
//             HandleDescriptor{member.name}
//         );

//         memberResource.SetHandle(memberPtr);

//         // Output the member resource
//         ctx.Out(i, memberResource);

//         NODE_LOG_DEBUG("[StructSpreaderNode::Compile] Output member " + std::to_string(i) + " as variadic output slot");
//     }

//     NODE_LOG_DEBUG("[StructSpreaderNode::Compile] Spread " + std::to_string(memberMetadata_.size()) + " members to outputs");
// }

// void StructSpreaderNode::ExecuteImpl(Context& ctx) {
//     // No per-frame work - struct spreading happens during Compile
// }

// void StructSpreaderNode::CleanupImpl(Context& ctx) {
//     structPtr_ = nullptr;
// }

// } // namespace Vixen::RenderGraph
