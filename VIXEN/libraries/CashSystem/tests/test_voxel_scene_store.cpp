// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.
//
// Shared voxel-scene store (T-5199): CPU scene entries persist under the shared cache directory,
// keyed by the generator fingerprints, so a fresh build tree with identical SVO code reuses them
// and a tree with different code cannot. These tests need no GPU: only the store is exercised.

#include <gtest/gtest.h>
#include <VoxelSceneCacher.h>
#include "ScopedEnvironment.h"
#include "SvoGeneratorFingerprint.h"
#include "VoxelSceneCacherFingerprint.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

using CashSystem::CacheFileStatus;
using CashSystem::SceneType;
using CashSystem::VoxelSceneCacher;
using CashSystem::VoxelSceneCreateInfo;
using CashSystem::VoxelSceneData;

namespace {

class VoxelSceneStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = std::filesystem::temp_directory_path() /
                ("vixen-voxel-scene-store-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) +
                 "-" + ::testing::UnitTest::GetInstance()->current_test_info()->name());
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_);
        environment_.emplace("VIXEN_SHARED_CACHE_DIR", root_.string().c_str());
    }
    void TearDown() override {
        environment_.reset();
        std::filesystem::remove_all(root_);
    }

    // Internally consistent synthetic scene: 3 nodes, a 2^3 brick grid.
    static VoxelSceneData MakeScene(const VoxelSceneCreateInfo& ci) {
        VoxelSceneData d;
        d.resolution = ci.resolution;
        d.sceneType = ci.sceneType;
        d.nodeCount = 3;
        d.esvoNodesCPU.assign(3 * sizeof(Vixen::SVO::ChildDescriptor), 0x5A);
        d.brickDataCPU.assign(100, 0x11);
        d.materialsCPU.resize(4);
        d.compressedColorsCPU.assign(32, 0x22);
        d.compressedNormalsCPU.assign(32, 0x33);
        d.configCPU.bricksPerAxis = 2;
        d.brickGridLookupCPU.assign(8, 7u);
        d.brickCount = 5;
        d.solidVoxelCount = 77;
        return d;
    }

    std::optional<Vixen::Core::Env::ScopedEnvironment> environment_;
    std::filesystem::path root_;
};

}  // namespace

TEST_F(VoxelSceneStoreTest, PathLivesInSharedStoreUnderBothGeneratorFingerprints) {
    VoxelSceneCreateInfo ci;
    const auto path = VoxelSceneCacher::SharedSceneEntryPath(ci);
    EXPECT_EQ(path.parent_path().parent_path().filename(), "voxel-scene");
    EXPECT_EQ(path.parent_path().parent_path().parent_path(), root_);
    const std::string generation = path.parent_path().filename().string();
    EXPECT_NE(generation.find(Vixen::SVO::kSvoGeneratorFingerprint), std::string::npos)
        << "SVO code identity must be part of the store location: " << generation;
    EXPECT_NE(generation.find(CashSystem::kVoxelSceneCacherFingerprint), std::string::npos)
        << "cacher code identity must be part of the store location: " << generation;
}

TEST_F(VoxelSceneStoreTest, StoreThenLoadRoundTripsEveryCpuBuffer) {
    VoxelSceneCreateInfo ci;
    ci.resolution = 64;
    const VoxelSceneData stored = MakeScene(ci);
    std::string error;
    ASSERT_TRUE(VoxelSceneCacher::StoreSharedScene(ci, stored, &error)) << error;

    VoxelSceneData loaded;
    ASSERT_EQ(VoxelSceneCacher::LoadSharedScene(ci, loaded, &error), CacheFileStatus::Loaded) << error;
    EXPECT_EQ(loaded.esvoNodesCPU, stored.esvoNodesCPU);
    EXPECT_EQ(loaded.brickDataCPU, stored.brickDataCPU);
    EXPECT_EQ(loaded.compressedColorsCPU, stored.compressedColorsCPU);
    EXPECT_EQ(loaded.compressedNormalsCPU, stored.compressedNormalsCPU);
    EXPECT_EQ(loaded.brickGridLookupCPU, stored.brickGridLookupCPU);
    EXPECT_EQ(loaded.materialsCPU.size(), stored.materialsCPU.size());
    EXPECT_EQ(loaded.configCPU.bricksPerAxis, 2);
    EXPECT_EQ(loaded.nodeCount, 3u);
    EXPECT_EQ(loaded.brickCount, 5u);
    EXPECT_EQ(loaded.solidVoxelCount, 77u);
    EXPECT_EQ(loaded.resolution, 64u);
}

TEST_F(VoxelSceneStoreTest, EmptyStoreIsMissingNotAnError) {
    VoxelSceneCreateInfo ci;
    VoxelSceneData loaded;
    EXPECT_EQ(VoxelSceneCacher::LoadSharedScene(ci, loaded), CacheFileStatus::Missing);
}

TEST_F(VoxelSceneStoreTest, DifferentSceneParametersUseDifferentEntries) {
    VoxelSceneCreateInfo a;
    a.resolution = 64;
    VoxelSceneCreateInfo b = a;
    b.seed = a.seed + 1;
    ASSERT_NE(VoxelSceneCacher::SharedSceneEntryPath(a), VoxelSceneCacher::SharedSceneEntryPath(b));
    ASSERT_TRUE(VoxelSceneCacher::StoreSharedScene(a, MakeScene(a)));
    VoxelSceneData loaded;
    EXPECT_EQ(VoxelSceneCacher::LoadSharedScene(b, loaded), CacheFileStatus::Missing);
}

TEST_F(VoxelSceneStoreTest, EntryFromOtherGeneratorCodeIsNeverServed) {
    // A tree with different SVO code has a different generation directory; model it by moving the
    // stored entry into a sibling generation directory and asking for the scene again.
    VoxelSceneCreateInfo ci;
    ci.resolution = 64;
    ASSERT_TRUE(VoxelSceneCacher::StoreSharedScene(ci, MakeScene(ci)));
    const auto path = VoxelSceneCacher::SharedSceneEntryPath(ci);
    const auto otherGeneration = path.parent_path().parent_path() / "0000000000000000-ffffffffffffffff";
    std::filesystem::create_directories(otherGeneration);
    std::filesystem::rename(path, otherGeneration / path.filename());

    VoxelSceneData loaded;
    EXPECT_EQ(VoxelSceneCacher::LoadSharedScene(ci, loaded), CacheFileStatus::Missing);
}

TEST_F(VoxelSceneStoreTest, CorruptEntryIsRejectedAndLeavesTargetUntouched) {
    VoxelSceneCreateInfo ci;
    ci.resolution = 64;
    ASSERT_TRUE(VoxelSceneCacher::StoreSharedScene(ci, MakeScene(ci)));
    const auto path = VoxelSceneCacher::SharedSceneEntryPath(ci);
    {
        std::fstream f(path, std::ios::binary | std::ios::in | std::ios::out);
        f.seekp(-4, std::ios::end);
        const char junk[4] = {'\x7f', '\x7f', '\x7f', '\x7f'};
        f.write(junk, 4);
    }
    VoxelSceneData loaded;
    std::string error;
    const auto status = VoxelSceneCacher::LoadSharedScene(ci, loaded, &error);
    EXPECT_TRUE(status == CacheFileStatus::Rejected || status == CacheFileStatus::IoError) << error;
    EXPECT_TRUE(loaded.esvoNodesCPU.empty());
}

TEST_F(VoxelSceneStoreTest, CustomGeneratorsNeverUseTheStore) {
    VoxelSceneCreateInfo ci;
    ci.sceneType = SceneType::Custom;
    ci.customGeneratorName = "alpha";
    ci.resolution = 64;
    std::string error;
    EXPECT_FALSE(VoxelSceneCacher::StoreSharedScene(ci, MakeScene(ci), &error));
    EXPECT_FALSE(error.empty());
    VoxelSceneData loaded;
    EXPECT_EQ(VoxelSceneCacher::LoadSharedScene(ci, loaded), CacheFileStatus::Missing);
}
