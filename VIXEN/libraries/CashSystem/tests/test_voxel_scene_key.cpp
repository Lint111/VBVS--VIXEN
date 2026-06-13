// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.
//
// Cache-key regression tests for VoxelSceneCreateInfo (AR#52).
//
// Before the fix, every SceneType::Custom scene collapsed to the same key
// (sceneType==255) and operator== treated distinct custom generators as equal,
// so they aliased to one cache entry and could not be told apart. These tests
// pin that distinct custom generator names produce distinct keys, while built-in
// scenes (empty name) keep their previous keys.

#include <gtest/gtest.h>
#include <VoxelSceneCacher.h>

using CashSystem::SceneType;
using CashSystem::VoxelSceneCreateInfo;

TEST(VoxelSceneCacheKey, DistinctCustomGeneratorsHaveDistinctKeys) {
    VoxelSceneCreateInfo a;
    a.sceneType = SceneType::Custom;
    a.customGeneratorName = "alpha";

    VoxelSceneCreateInfo b = a;  // identical resolution/density/seed
    b.customGeneratorName = "beta";

    EXPECT_NE(a.ComputeHash(), b.ComputeHash())
        << "Distinct custom generators must not share a cache key.";
    EXPECT_FALSE(a == b)
        << "Distinct custom generators must not compare equal.";
}

TEST(VoxelSceneCacheKey, SameCustomGeneratorSameKey) {
    VoxelSceneCreateInfo a;
    a.sceneType = SceneType::Custom;
    a.customGeneratorName = "alpha";

    VoxelSceneCreateInfo b = a;

    EXPECT_EQ(a.ComputeHash(), b.ComputeHash());
    EXPECT_TRUE(a == b);
}

// Built-in scenes have an empty customGeneratorName, so their cache keys must be
// identical to the legacy formula (no key churn, existing on-disk caches hit).
TEST(VoxelSceneCacheKey, BuiltinKeysUnchangedByNameField) {
    VoxelSceneCreateInfo cornell;  // CornellBox, empty customGeneratorName

    const uint32_t densityQuantized = static_cast<uint32_t>(cornell.density * 100.0f);
    uint64_t legacy = static_cast<uint64_t>(cornell.sceneType);
    legacy = legacy * 31 + cornell.resolution;
    legacy = legacy * 31 + densityQuantized;
    legacy = legacy * 31 + cornell.seed;

    EXPECT_EQ(cornell.ComputeHash(), legacy)
        << "An empty custom name must not change a built-in scene's key.";
}
