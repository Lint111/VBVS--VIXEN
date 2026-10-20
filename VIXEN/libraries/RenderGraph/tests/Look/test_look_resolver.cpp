// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#include "Look/LookResolver.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace Vixen::RenderGraph;

namespace {

LookContribution Contribute(LookLayer layer, PartialLook look, float weight = 1.0f, int priority = 0) {
    LookContribution c;
    c.layer = layer;
    c.look = std::move(look);
    c.weight = weight;
    c.priority = priority;
    return c;
}

PartialLook Saturation(float value) {
    PartialLook look;
    look.saturation = value;
    return look;
}

PartialLook Compensation(float ev) {
    PartialLook look;
    look.exposureCompensationEV = ev;
    return look;
}

PartialLook Window(float minEV, float maxEV) {
    PartialLook look;
    look.exposureMinEV100 = minEV;
    look.exposureMaxEV100 = maxEV;
    return look;
}

}  // namespace

TEST(LookResolver, EmptyResolverIsTheNodeDefaults) {
    const ResolvedLook look = LookResolver{}.Resolve();
    EXPECT_EQ(look.saturation, 1.0f);
    EXPECT_EQ(look.exposureCompensationEV, 0.0f);
    EXPECT_EQ(look.exposureKeyValue, 0.18f);
    EXPECT_EQ(look.exposureMinEV100, -8.0f);
    EXPECT_EQ(look.exposureMaxEV100, 12.0f);
    EXPECT_EQ(look.exposureSpeedUp, 3.0f);
    EXPECT_EQ(look.exposureSpeedDown, 1.0f);
    EXPECT_FALSE(look.lights.has_value());
}

TEST(LookResolver, LayersOverrideInOrderRegardlessOfInsertion) {
    LookResolver resolver;
    resolver.Add(Contribute(LookLayer::DevOverride, Saturation(0.25f)));
    resolver.Add(Contribute(LookLayer::Scene, Saturation(1.5f)));
    resolver.Add(Contribute(LookLayer::Flow, Saturation(0.5f)));
    resolver.Add(Contribute(LookLayer::Volume, Saturation(0.75f)));
    EXPECT_EQ(resolver.Resolve().saturation, 0.25f);
}

TEST(LookResolver, EachLayerWinsOverTheOnesBelowIt) {
    LookResolver resolver;
    const auto scene = resolver.Add(Contribute(LookLayer::Scene, Saturation(1.5f)));
    EXPECT_EQ(resolver.Resolve().saturation, 1.5f);
    const auto volume = resolver.Add(Contribute(LookLayer::Volume, Saturation(0.75f)));
    EXPECT_EQ(resolver.Resolve().saturation, 0.75f);
    const auto flow = resolver.Add(Contribute(LookLayer::Flow, Saturation(0.5f)));
    EXPECT_EQ(resolver.Resolve().saturation, 0.5f);
    EXPECT_TRUE(resolver.Remove(flow));
    EXPECT_EQ(resolver.Resolve().saturation, 0.75f);
    EXPECT_TRUE(resolver.Remove(volume));
    EXPECT_EQ(resolver.Resolve().saturation, 1.5f);
    EXPECT_TRUE(resolver.Remove(scene));
    EXPECT_EQ(resolver.Resolve().saturation, 1.0f);
    EXPECT_FALSE(resolver.Remove(scene));
}

TEST(LookResolver, HigherPriorityWinsWithinALayerAndTiesKeepInsertionOrder) {
    LookResolver resolver;
    resolver.Add(Contribute(LookLayer::Volume, Saturation(0.5f), 1.0f, 5));
    resolver.Add(Contribute(LookLayer::Volume, Saturation(1.75f), 1.0f, 1));
    EXPECT_EQ(resolver.Resolve().saturation, 0.5f);

    LookResolver ties;
    ties.Add(Contribute(LookLayer::Volume, Saturation(0.5f), 1.0f, 2));
    ties.Add(Contribute(LookLayer::Volume, Saturation(1.75f), 1.0f, 2));
    EXPECT_EQ(ties.Resolve().saturation, 1.75f);
}

TEST(LookResolver, WeightBlendsTowardTheContributionAndZeroLeavesTheLowerResult) {
    LookResolver resolver;
    resolver.Add(Contribute(LookLayer::Scene, Saturation(1.0f)));
    const auto volume = resolver.Add(Contribute(LookLayer::Volume, Saturation(0.0f), 0.25f));
    EXPECT_FLOAT_EQ(resolver.Resolve().saturation, 0.75f);
    resolver.Set(volume, Contribute(LookLayer::Volume, Saturation(0.0f), 0.0f));
    EXPECT_EQ(resolver.Resolve().saturation, 1.0f);
    resolver.Set(volume, Contribute(LookLayer::Volume, Saturation(0.0f), 1.0f));
    EXPECT_EQ(resolver.Resolve().saturation, 0.0f);
    resolver.Set(volume, Contribute(LookLayer::Volume, Saturation(0.0f), 7.0f));
    EXPECT_EQ(resolver.Resolve().saturation, 0.0f);
}

TEST(LookResolver, UnsetChannelsAreLeftAlone) {
    LookResolver resolver;
    PartialLook scene = Saturation(1.5f);
    scene.exposureCompensationEV = -0.5f;
    resolver.Add(Contribute(LookLayer::Scene, scene));
    resolver.Add(Contribute(LookLayer::DevOverride, Compensation(2.0f)));
    const ResolvedLook look = resolver.Resolve();
    EXPECT_EQ(look.saturation, 1.5f);
    EXPECT_EQ(look.exposureCompensationEV, 2.0f);
}

TEST(LookResolver, ExposureLikeChannelsBlendInEvSpace) {
    LookResolver resolver;
    resolver.Add(Contribute(LookLayer::Scene, Compensation(-2.0f)));
    resolver.Add(Contribute(LookLayer::Volume, Compensation(2.0f), 0.5f));
    EXPECT_FLOAT_EQ(resolver.Resolve().exposureCompensationEV, 0.0f);

    // EV lerp is a log2 lerp of the exposure multiplier: the midpoint multiplier is the geometric mean.
    const float mid = std::exp2(resolver.Resolve().exposureCompensationEV);
    EXPECT_FLOAT_EQ(mid, std::sqrt(std::exp2(-2.0f) * std::exp2(2.0f)));

    LookResolver window;
    window.Add(Contribute(LookLayer::Scene, Window(-8.0f, 12.0f)));
    window.Add(Contribute(LookLayer::Volume, Window(0.0f, 4.0f), 0.5f));
    const ResolvedLook look = window.Resolve();
    EXPECT_FLOAT_EQ(look.exposureMinEV100, -4.0f);
    EXPECT_FLOAT_EQ(look.exposureMaxEV100, 8.0f);
}

TEST(LookResolver, KeyValueBlendsInLog2AndSaturationInLinearSpace) {
    LookResolver resolver;
    PartialLook scene;
    scene.exposureKeyValue = 0.1f;
    scene.saturation = 0.0f;
    PartialLook volume;
    volume.exposureKeyValue = 0.4f;
    volume.saturation = 2.0f;
    resolver.Add(Contribute(LookLayer::Scene, scene));
    resolver.Add(Contribute(LookLayer::Volume, volume, 0.5f));
    const ResolvedLook look = resolver.Resolve();
    EXPECT_NEAR(look.exposureKeyValue, std::sqrt(0.1f * 0.4f), 1.0e-6f);   // geometric, not 0.25
    EXPECT_FLOAT_EQ(look.saturation, 1.0f);                                 // arithmetic
}

TEST(LookResolver, DiscreteChannelsNeverBlend) {
    LookResolver resolver;
    PartialLook scene;
    scene.skyMode = 0u;
    PartialLook volume;
    volume.skyMode = 1u;
    resolver.Add(Contribute(LookLayer::Scene, scene));
    const auto handle = resolver.Add(Contribute(LookLayer::Volume, volume, 0.4f));
    EXPECT_EQ(resolver.Resolve().skyMode, 0u);
    resolver.Set(handle, Contribute(LookLayer::Volume, volume, 0.6f));
    EXPECT_EQ(resolver.Resolve().skyMode, 1u);

    LookLightSet lights;
    lights.lightCount = 1;
    lights.ambientIntensity = 0.04f;
    PartialLook lit;
    lit.lights = lights;
    resolver.Add(Contribute(LookLayer::Flow, lit, 0.3f));
    EXPECT_FALSE(resolver.Resolve().lights.has_value());
    resolver.Add(Contribute(LookLayer::Flow, lit, 1.0f));
    ASSERT_TRUE(resolver.Resolve().lights.has_value());
    EXPECT_EQ(resolver.Resolve().lights->ambientIntensity, 0.04f);
}

TEST(LookResolver, FixedExposureSetsOnlyTheWindowAndSnapsWhenTheWindowIsClosed) {
    LookResolver resolver;
    PartialLook autoScene = Window(-8.0f, 12.0f);
    autoScene.exposureSpeedUp = 3.0f;
    autoScene.exposureSpeedDown = 1.0f;
    resolver.Add(Contribute(LookLayer::Scene, autoScene));

    // A half-weight Fixed layer narrows the window; the Auto speeds stay (no discontinuity at the snap).
    const auto fixed = resolver.Add(Contribute(LookLayer::Volume, Window(2.0f, 2.0f), 0.5f));
    ResolvedLook look = resolver.Resolve();
    EXPECT_FLOAT_EQ(look.exposureMinEV100, -3.0f);
    EXPECT_FLOAT_EQ(look.exposureMaxEV100, 7.0f);
    EXPECT_EQ(look.exposureSpeedUp, 3.0f);
    EXPECT_EQ(look.exposureSpeedDown, 1.0f);

    // Full weight closes the window: only then does the meter snap (speed 0).
    resolver.Set(fixed, Contribute(LookLayer::Volume, Window(2.0f, 2.0f), 1.0f));
    look = resolver.Resolve();
    EXPECT_EQ(look.exposureMinEV100, 2.0f);
    EXPECT_EQ(look.exposureMaxEV100, 2.0f);
    EXPECT_EQ(look.exposureSpeedUp, 0.0f);
    EXPECT_EQ(look.exposureSpeedDown, 0.0f);
    EXPECT_EQ(look.exposureKeyValue, 0.18f);
    EXPECT_EQ(look.exposureLowPercent, 50.0f);
    EXPECT_EQ(look.exposureHighPercent, 95.0f);
}

TEST(LookResolver, RelativeModeIsOffByDefaultAndAddsInTheChannelSpaceWhenRequested) {
    EXPECT_FALSE(LookContribution{}.relative);

    LookResolver resolver;
    resolver.Add(Contribute(LookLayer::Scene, Compensation(-1.0f)));
    LookContribution bias = Contribute(LookLayer::Volume, Compensation(0.5f), 1.0f);
    const auto handle = resolver.Add(bias);
    EXPECT_EQ(resolver.Resolve().exposureCompensationEV, 0.5f);   // override, not add

    bias.relative = true;
    resolver.Set(handle, bias);
    EXPECT_FLOAT_EQ(resolver.Resolve().exposureCompensationEV, -0.5f);
    bias.weight = 0.5f;
    resolver.Set(handle, bias);
    EXPECT_FLOAT_EQ(resolver.Resolve().exposureCompensationEV, -0.75f);
}

TEST(LookResolver, SlotsReplaceInPlaceAndKeepTheirLayer) {
    LookResolver resolver;
    resolver.SetSlot(LookLayer::Scene, 0, Contribute(LookLayer::DevOverride, Saturation(0.5f)));
    resolver.SetSlot(LookLayer::Scene, 0, Contribute(LookLayer::DevOverride, Saturation(1.5f)));
    EXPECT_EQ(resolver.Size(), 1u);
    resolver.Add(Contribute(LookLayer::Volume, Saturation(0.25f)));
    EXPECT_EQ(resolver.Resolve().saturation, 0.25f);   // the slot is a Scene contribution whatever the struct said
}

TEST(LookResolver, LightSetClipsToCapacityAndDefaultsMissingScales) {
    std::vector<Vixen::Gpu::Light> lights(6);
    const LookLightSet set = LookLightSet::From(lights, 0.2f, {2.0f});
    EXPECT_EQ(set.lightCount, 4u);
    EXPECT_EQ(set.celSpillPurposeScales[0], 2.0f);
    EXPECT_EQ(set.celSpillPurposeScales[1], 1.0f);
    EXPECT_EQ(set.ambientIntensity, 0.2f);
}
