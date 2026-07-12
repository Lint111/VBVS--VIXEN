#include <gtest/gtest.h>
#include "Core/NodeTypeRegistry.h"
#include "Core/NodeRegistration.h"
// Type-keyed spot-checks (robust — no fragile name strings):
#include "Nodes/CameraNode.h"
#include "Nodes/DeviceNode.h"
#include "Nodes/VoxelGridNode.h"
#include "Nodes/PresentNode.h"
#include "Nodes/BufferSyncGathererNode.h"  // Sampled Lighting Inc3 M5: variadic VkBuffer sync gatherer

using namespace Vixen::RenderGraph;

// If RenderGraphNodes is NOT whole-archived, the per-node static registrars are
// stripped by the linker and the registry comes back nearly empty — this test is
// the guard that catches that (and confirms RegisterAllNodes works at all).
TEST(NodeSelfRegistration, RegistersAllBuiltInNodes) {
    NodeTypeRegistry registry;
    RegisterAllNodes(registry);

    // 40+ node types self-register. Use >= so adding a node never breaks the test;
    // the stripping bug drives this to ~0.
    EXPECT_GE(registry.GetNodeTypeCount(), 32u);

    // Stable, type-keyed lookups across node groups:
    EXPECT_TRUE(registry.Has<CameraNodeType>());
    EXPECT_TRUE(registry.Has<DeviceNodeType>());
    EXPECT_TRUE(registry.Has<VoxelGridNodeType>());
    EXPECT_TRUE(registry.Has<PresentNodeType>());
    EXPECT_TRUE(registry.Has<BufferSyncGathererNodeType>());
}

// RegisterAllNodes replays into whatever registry instance it is given — proves
// the manifest is per-EngineContext-friendly (two independent registries).
TEST(NodeSelfRegistration, ReplaysIntoEachRegistryInstance) {
    NodeTypeRegistry a;
    NodeTypeRegistry b;
    RegisterAllNodes(a);
    RegisterAllNodes(b);
    EXPECT_EQ(a.GetNodeTypeCount(), b.GetNodeTypeCount());
    EXPECT_GE(a.GetNodeTypeCount(), 32u);
}
