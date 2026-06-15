// AR#32 — alpha blending: cache-key correctness for the per-pipeline blend state.
//
// PipelineCreateParams is the graphics-pipeline cache key. If blend state is not part of
// ComputeKey, two pipelines differing only in blend mode collide on one cached VkPipeline
// (the first-built blend mode silently wins for both). These tests guard against that and
// against a regression in the critical default (opaque, write-RGBA).

#include <gtest/gtest.h>
#include <PipelineCacher.h>

namespace {

// Expose the protected ComputeKey override for direct testing (it is a pure function of the
// params — no device needed).
struct KeyProbe : CashSystem::PipelineCacher {
    using CashSystem::PipelineCacher::ComputeKey;
};

CashSystem::PipelineCreateParams BaseParams() {
    CashSystem::PipelineCreateParams p;
    p.vertexShaderKey = "vs";
    p.fragmentShaderKey = "fs";
    p.layoutKey = "layout";
    p.renderPassKey = "rp";
    return p;  // colorBlendAttachment defaults to opaque / write-RGBA
}

VkPipelineColorBlendAttachmentState AlphaBlend() {
    VkPipelineColorBlendAttachmentState s{};
    s.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                       VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    s.blendEnable = VK_TRUE;
    s.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    s.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    s.colorBlendOp = VK_BLEND_OP_ADD;
    s.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    s.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    s.alphaBlendOp = VK_BLEND_OP_ADD;
    return s;
}

VkPipelineColorBlendAttachmentState AdditiveBlend() {
    auto s = AlphaBlend();
    s.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;  // differs from Alpha only in dst factors
    s.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    return s;
}

} // namespace

TEST(PipelineBlendKey, BlendModeChangesCacheKey) {
    KeyProbe probe;
    auto opaque = BaseParams();
    auto alpha = BaseParams();
    alpha.colorBlendAttachment = AlphaBlend();
    EXPECT_NE(probe.ComputeKey(opaque), probe.ComputeKey(alpha));
}

TEST(PipelineBlendKey, DistinctBlendModesGiveDistinctKeys) {
    KeyProbe probe;
    auto opaque = BaseParams();
    auto alpha = BaseParams();    alpha.colorBlendAttachment = AlphaBlend();
    auto additive = BaseParams(); additive.colorBlendAttachment = AdditiveBlend();

    const auto kOpaque = probe.ComputeKey(opaque);
    const auto kAlpha = probe.ComputeKey(alpha);
    const auto kAdditive = probe.ComputeKey(additive);
    EXPECT_NE(kOpaque, kAlpha);
    EXPECT_NE(kOpaque, kAdditive);
    EXPECT_NE(kAlpha, kAdditive);
}

TEST(PipelineBlendKey, SameBlendGivesSameKey) {
    KeyProbe probe;
    auto a = BaseParams(); a.colorBlendAttachment = AlphaBlend();
    auto b = BaseParams(); b.colorBlendAttachment = AlphaBlend();
    EXPECT_EQ(probe.ComputeKey(a), probe.ComputeKey(b));
}

TEST(PipelineBlendKey, DefaultParamsAreOpaqueWriteRGBA) {
    // A fresh PipelineCreateParams must write all channels with blending off. A zero-init
    // would set colorWriteMask=0 and render nothing — this guards that the default is correct.
    CashSystem::PipelineCreateParams p;
    EXPECT_EQ(p.colorBlendAttachment.blendEnable, VK_FALSE);
    EXPECT_EQ(p.colorBlendAttachment.colorWriteMask,
              static_cast<VkColorComponentFlags>(
                  VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT));
}
