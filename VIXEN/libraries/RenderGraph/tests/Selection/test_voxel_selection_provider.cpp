// SEL-P2 — VoxelSelectionProvider headless behavior.
//
// These cover the parts of the provider that need NO Vulkan device:
//   - priority() / kind() contract (world layer, Voxel domain),
//   - resolve() returns nullopt when unconfigured (no device/pool/ID image),
//   - resolve() returns nullopt for a zero-sized viewport.
// The actual GPU readback (configure() + a real ID image + a hit decode) is
// exercised live in the running app (the user's manual click test) — it requires
// a device, queue and the pick-ID image, which are out of scope for a unit test.

#include <gtest/gtest.h>

#include "Selection/VoxelSelectionProvider.h"
#include "Selection/SelectContext.h"
#include "Selection/SelectionId.h"

using Vixen::RenderGraph::VoxelSelectionProvider;
using Vixen::RenderGraph::SelectContext;
using Vixen::RenderGraph::ProviderKind;

namespace {

SelectContext MakeCtx(uint32_t w, uint32_t h) {
    SelectContext ctx{};
    ctx.screenPoint    = { static_cast<float>(w) * 0.5f, static_cast<float>(h) * 0.5f };
    ctx.viewportWidth  = w;
    ctx.viewportHeight = h;
    ctx.camera         = nullptr;  // voxel provider ignores the camera
    ctx.modifier       = Vixen::RenderGraph::SelectionModifier::Replace;
    ctx.button         = 0;
    return ctx;
}

} // namespace

TEST(VoxelSelectionProvider, KindIsVoxel) {
    VoxelSelectionProvider p;
    EXPECT_EQ(p.kind(), ProviderKind::Voxel);
}

TEST(VoxelSelectionProvider, PriorityIsWorldLayerLow) {
    VoxelSelectionProvider p;
    // World layer — providers are sorted descending, so UI (higher) is queried first.
    // The concrete value is an implementation detail; assert it is the low (0) world layer.
    EXPECT_EQ(p.priority(), 0);
}

TEST(VoxelSelectionProvider, ResolveWithoutConfigureIsMiss) {
    // No configure() → no device/pool/ID image → must report a miss (nullopt), never crash.
    VoxelSelectionProvider p;
    auto hit = p.resolve(MakeCtx(1920, 1080));
    EXPECT_FALSE(hit.has_value());
}

TEST(VoxelSelectionProvider, ResolveWithZeroViewportIsMiss) {
    VoxelSelectionProvider p;
    auto hit = p.resolve(MakeCtx(0, 0));
    EXPECT_FALSE(hit.has_value());
}
