// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
// auto-sync P4 M3: smoke test for PassGroupNode (no GPU / no device required)

#include <gtest/gtest.h>
#include "Core/NodeTypeRegistry.h"
#include "Core/NodeRegistration.h"
#include "Nodes/PassGroupNode.h"

using namespace Vixen::RenderGraph;

// ============================================================================
// 1. Registry lookup — verifies VIXEN_REGISTER_NODE fired correctly and the
//    node is creatable by type name (whole-archive guard).
// ============================================================================

TEST(PassGroupNodeSmoke, RegistryLookupByType) {
    NodeTypeRegistry registry;
    RegisterAllNodes(registry);

    EXPECT_TRUE(registry.Has<PassGroupNodeType>())
        << "PassGroupNodeType must be self-registered via VIXEN_REGISTER_NODE";

    PassGroupNodeType* nodeType = registry.Get<PassGroupNodeType>();
    ASSERT_NE(nodeType, nullptr);

    auto instance = nodeType->CreateInstance("smoke_pass_group");
    ASSERT_NE(instance, nullptr) << "CreateInstance must return a non-null PassGroupNode";
}

// ============================================================================
// 2. Host assembly API — verifies AddComputePass / AddRenderPass grow the list.
//    No GPU, no Vulkan device; all Vulkan handles remain VK_NULL_HANDLE.
// ============================================================================

TEST(PassGroupNodeSmoke, AddPassGrowsPassCount) {
    // Direct construction (no registry, no device)
    PassGroupNodeType nodeType;
    PassGroupNode node("test_node", &nodeType);

    EXPECT_EQ(node.PassCount(), 0u);

    node.AddComputePass(ComputePassStep{});
    EXPECT_EQ(node.PassCount(), 1u);

    node.AddRenderPass(RenderPassStep{});
    EXPECT_EQ(node.PassCount(), 2u);
}

TEST(PassGroupNodeSmoke, SetPassesReplacesExisting) {
    PassGroupNodeType nodeType;
    PassGroupNode node("set_passes_node", &nodeType);

    node.AddComputePass(ComputePassStep{});
    ASSERT_EQ(node.PassCount(), 1u);

    std::vector<PassStep> batch;
    batch.emplace_back(ComputePassStep{});
    batch.emplace_back(RenderPassStep{});
    batch.emplace_back(ComputePassStep{});
    node.SetPasses(std::move(batch));

    EXPECT_EQ(node.PassCount(), 3u);
}
