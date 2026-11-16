#include "GpuChunkCostCacher.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>

namespace {

CashSystem::GpuChunkCostCacheCreateInfo MakeInfo(std::string scene) {
    CashSystem::GpuChunkCostCacheCreateInfo info;
    info.sceneIdentity = std::move(scene);
    info.passIdentity = "test_dispatch";
    info.extentX = 240;
    info.extentY = 135;
    info.extentZ = 1;
    info.qualityProfile = "scale=1.000000;upscaler=0;aa=taa";
    info.deviceIdentity = 0x1234;
    return info;
}

std::filesystem::path UniqueCachePath() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
        ("vixen-gpu-chunk-cost-" + std::to_string(stamp) + ".cache");
}

} // namespace

TEST(GpuChunkCostCacherTest, DifferentScenesProduceDifferentEntries) {
    CashSystem::GpuChunkCostCacher cacher;
    auto latin = MakeInfo("glyphsdf|variant=latin-carved");
    auto rtl = MakeInfo("glyphsdf|variant=rtl-proof-pair");

    EXPECT_NE(CashSystem::GpuChunkCostCacher::KeyForTests(latin),
              CashSystem::GpuChunkCostCacher::KeyForTests(rtl));
    cacher.Store(latin, {14400, 128.5, 12000.0, 40.0, 100.0});
    cacher.Store(rtl, {14400, 256.0, 12000.0, 80.0, 100.0});

    ASSERT_NE(cacher.Lookup(latin), nullptr);
    ASSERT_NE(cacher.Lookup(rtl), nullptr);
    EXPECT_DOUBLE_EQ(cacher.Lookup(latin)->unitNs, 128.5);
    EXPECT_DOUBLE_EQ(cacher.Lookup(rtl)->unitNs, 256.0);
}

TEST(GpuChunkCostCacherTest, SameConfigurationReusesAcrossSerializedRuns) {
    const auto path = UniqueCachePath();
    const auto config = MakeInfo("rtl-proof");
    {
        CashSystem::GpuChunkCostCacher cold;
        cold.Store(config, {32400, 91.0, 5000.0, 44.0, 32.0});
        ASSERT_TRUE(cold.SerializeToFile(path));
    }

    CashSystem::GpuChunkCostCacher warm;
    ASSERT_TRUE(warm.DeserializeFromFile(path, nullptr));
    const auto restored = warm.Lookup(config);
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->anchorUnits, 32400u);
    EXPECT_DOUBLE_EQ(restored->unitNs, 91.0);
    EXPECT_DOUBLE_EQ(restored->meanFixedNs, 32.0);
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(GpuChunkCostCacherTest, DeliberateKeyBreakDoesNotReuseCalibration) {
    CashSystem::GpuChunkCostCacher cacher;
    const auto original = MakeInfo("scene-a");
    auto changed = original;
    changed.extentZ = 2;
    cacher.Store(original, {100, 10.0, 1.0, 10.0, 1.0});

    EXPECT_NE(CashSystem::GpuChunkCostCacher::KeyForTests(original),
              CashSystem::GpuChunkCostCacher::KeyForTests(changed));
    EXPECT_EQ(cacher.Lookup(changed), nullptr);
}
