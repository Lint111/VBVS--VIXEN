// AR#32 — alpha blending: BLEND_MODE parameter for GraphicsPipelineNode.
//
// Pure unit tests for the blend-recipe helper (MakeColorBlendAttachment) and the
// BLEND_MODE config constant. No Vulkan device required — the recipe is a pure
// string -> VkPipelineColorBlendAttachmentState mapping that is the single source
// of truth for both the cacher path and the manual fallback path.

#include <gtest/gtest.h>
#include <stdexcept>

#include "NodeHelpers/VulkanStructHelpers.h"
#include "Data/Nodes/GraphicsPipelineNodeConfig.h"

using RenderGraph::NodeHelpers::MakeColorBlendAttachment;

namespace {

constexpr VkColorComponentFlags kRGBA =
    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

} // namespace

// ---- "None" preserves today's opaque behavior exactly --------------------

TEST(BlendMode, NoneIsOpaqueDisabled) {
    auto s = MakeColorBlendAttachment("None");
    EXPECT_EQ(s.blendEnable, VK_FALSE);
    EXPECT_EQ(s.colorWriteMask, kRGBA);
}

// All modes write all color channels.
TEST(BlendMode, AllModesWriteRGBA) {
    for (const char* mode : {"None", "Alpha", "PremultipliedAlpha", "Additive", "Multiply"}) {
        EXPECT_EQ(MakeColorBlendAttachment(mode).colorWriteMask, kRGBA) << "mode=" << mode;
    }
}

// ---- Standard straight (non-premultiplied) alpha "over" ------------------

TEST(BlendMode, Alpha) {
    auto s = MakeColorBlendAttachment("Alpha");
    EXPECT_EQ(s.blendEnable, VK_TRUE);
    EXPECT_EQ(s.srcColorBlendFactor, VK_BLEND_FACTOR_SRC_ALPHA);
    EXPECT_EQ(s.dstColorBlendFactor, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
    EXPECT_EQ(s.colorBlendOp, VK_BLEND_OP_ADD);
    EXPECT_EQ(s.srcAlphaBlendFactor, VK_BLEND_FACTOR_ONE);
    EXPECT_EQ(s.dstAlphaBlendFactor, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
    EXPECT_EQ(s.alphaBlendOp, VK_BLEND_OP_ADD);
}

// ---- Premultiplied alpha "over" (src already multiplied by its alpha) ----

TEST(BlendMode, PremultipliedAlpha) {
    auto s = MakeColorBlendAttachment("PremultipliedAlpha");
    EXPECT_EQ(s.blendEnable, VK_TRUE);
    EXPECT_EQ(s.srcColorBlendFactor, VK_BLEND_FACTOR_ONE);
    EXPECT_EQ(s.dstColorBlendFactor, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
    EXPECT_EQ(s.colorBlendOp, VK_BLEND_OP_ADD);
    EXPECT_EQ(s.srcAlphaBlendFactor, VK_BLEND_FACTOR_ONE);
    EXPECT_EQ(s.dstAlphaBlendFactor, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
    EXPECT_EQ(s.alphaBlendOp, VK_BLEND_OP_ADD);
}

// ---- Alpha-weighted additive (textured glow / fire / particles) ----------

TEST(BlendMode, Additive) {
    auto s = MakeColorBlendAttachment("Additive");
    EXPECT_EQ(s.blendEnable, VK_TRUE);
    EXPECT_EQ(s.srcColorBlendFactor, VK_BLEND_FACTOR_SRC_ALPHA);
    EXPECT_EQ(s.dstColorBlendFactor, VK_BLEND_FACTOR_ONE);
    EXPECT_EQ(s.colorBlendOp, VK_BLEND_OP_ADD);
    EXPECT_EQ(s.srcAlphaBlendFactor, VK_BLEND_FACTOR_ONE);
    EXPECT_EQ(s.dstAlphaBlendFactor, VK_BLEND_FACTOR_ONE);
    EXPECT_EQ(s.alphaBlendOp, VK_BLEND_OP_ADD);
}

// ---- Multiply / modulate (darkening) -------------------------------------

TEST(BlendMode, Multiply) {
    auto s = MakeColorBlendAttachment("Multiply");
    EXPECT_EQ(s.blendEnable, VK_TRUE);
    EXPECT_EQ(s.srcColorBlendFactor, VK_BLEND_FACTOR_DST_COLOR);
    EXPECT_EQ(s.dstColorBlendFactor, VK_BLEND_FACTOR_ZERO);
    EXPECT_EQ(s.colorBlendOp, VK_BLEND_OP_ADD);
    EXPECT_EQ(s.srcAlphaBlendFactor, VK_BLEND_FACTOR_DST_ALPHA);
    EXPECT_EQ(s.dstAlphaBlendFactor, VK_BLEND_FACTOR_ZERO);
    EXPECT_EQ(s.alphaBlendOp, VK_BLEND_OP_ADD);
}

// ---- Unknown modes are a hard error (matches Parse* convention) ----------

TEST(BlendMode, UnknownModeThrows) {
    EXPECT_THROW(MakeColorBlendAttachment("Screen"), std::runtime_error);
    EXPECT_THROW(MakeColorBlendAttachment(""), std::runtime_error);
    EXPECT_THROW(MakeColorBlendAttachment("alpha"), std::runtime_error);  // case-sensitive
}

// ---- Config constant exists and is the documented parameter name ----------

TEST(BlendMode, ConfigConstant) {
    EXPECT_STREQ(Vixen::RenderGraph::GraphicsPipelineNodeConfig::BLEND_MODE, "blendMode");
}
