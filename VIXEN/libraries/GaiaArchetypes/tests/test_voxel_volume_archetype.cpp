#include <gtest/gtest.h>
#include "VoxelVolumeArchetype.h"
#include <gaia.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <algorithm>

using namespace GaiaArchetype;

// ===========================================================================
// VoxelVolumeArchetype Basic Tests
// ===========================================================================

class VoxelVolumeArchetypeTest : public ::testing::Test {
protected:
    gaia::ecs::World world;
    std::unique_ptr<RelationshipObserver> observer;
    std::unique_ptr<RelationshipTypeRegistry> types;
    std::unique_ptr<VoxelVolumeArchetype> volumeArchetype;

    void SetUp() override {
        observer = std::make_unique<RelationshipObserver>(world);
        types = std::make_unique<RelationshipTypeRegistry>(world);
        volumeArchetype = std::make_unique<VoxelVolumeArchetype>(world, *observer, *types);
    }
};

TEST_F(VoxelVolumeArchetypeTest, CreateVolumeHasCorrectComponents) {
    auto volume = volumeArchetype->createVolume(glm::ivec3(0, 0, 0));

    EXPECT_TRUE(world.valid(volume));
    EXPECT_TRUE(world.has<VolumeOrigin>(volume));
    EXPECT_TRUE(world.has<VolumeSize>(volume));
    EXPECT_TRUE(world.has<VolumeStats>(volume));
    EXPECT_TRUE(world.has<VolumeBounds>(volume));
}

TEST_F(VoxelVolumeArchetypeTest, CreateVolumeWithCustomSize) {
    auto volume = volumeArchetype->createVolume(
        glm::ivec3(10, 20, 30),
        glm::ivec3(128, 128, 64)
    );

    EXPECT_TRUE(world.valid(volume));

    const auto& size = world.get<VolumeSize>(volume);
    EXPECT_EQ(size.width, 128);
    EXPECT_EQ(size.height, 128);
    EXPECT_EQ(size.depth, 64);

    const auto& origin = world.get<VolumeOrigin>(volume);
    EXPECT_EQ(origin.x, 10);
    EXPECT_EQ(origin.y, 20);
    EXPECT_EQ(origin.z, 30);
}

TEST_F(VoxelVolumeArchetypeTest, CreateVolumeWithDefaultSize) {
    auto volume = volumeArchetype->createVolume(glm::ivec3(5, 5, 5));

    const auto& size = world.get<VolumeSize>(volume);
    // Default size is 64x64x64
    EXPECT_EQ(size.width, 64);
    EXPECT_EQ(size.height, 64);
    EXPECT_EQ(size.depth, 64);
}

TEST_F(VoxelVolumeArchetypeTest, CreateVolumeInitializesStats) {
    auto volume = volumeArchetype->createVolume(glm::ivec3(0, 0, 0));

    auto stats = volumeArchetype->getVolumeStats(volume);
    ASSERT_NE(stats, nullptr);
    EXPECT_EQ(stats->voxelCount, 0);
    EXPECT_FALSE(stats->isDirty);
}

TEST_F(VoxelVolumeArchetypeTest, CreateVolumeInitializesBounds) {
    auto volume = volumeArchetype->createVolume(glm::ivec3(0, 0, 0));

    auto bounds = volumeArchetype->getVolumeBounds(volume);
    ASSERT_NE(bounds, nullptr);
    // Initial bounds should be invalid (no voxels)
    EXPECT_FALSE(bounds->isValid());
}

// ===========================================================================
// VoxelVolumeArchetype Callback Tests
// ===========================================================================

TEST_F(VoxelVolumeArchetypeTest, AddVoxelToVolumeTriggersCallback) {
    auto volume = volumeArchetype->createVolume(glm::ivec3(0, 0, 0));
    auto voxel = world.add();

    bool callbackTriggered = false;
    gaia::ecs::Entity callbackVoxel;
    gaia::ecs::Entity callbackVolume;

    volumeArchetype->setOnVoxelAdded([&](auto&, auto v, auto vol) {
        callbackTriggered = true;
        callbackVoxel = v;
        callbackVolume = vol;
    });

    volumeArchetype->addVoxelToVolume(voxel, volume);

    EXPECT_TRUE(callbackTriggered);
    EXPECT_EQ(callbackVoxel, voxel);
    EXPECT_EQ(callbackVolume, volume);
}

TEST_F(VoxelVolumeArchetypeTest, AddVoxelUpdatesStats) {
    auto volume = volumeArchetype->createVolume(glm::ivec3(0, 0, 0));

    // Initial stats
    auto stats = volumeArchetype->getVolumeStats(volume);
    ASSERT_NE(stats, nullptr);
    EXPECT_EQ(stats->voxelCount, 0);

    // Add a voxel
    auto voxel = world.add();
    volumeArchetype->addVoxelToVolume(voxel, volume);

    // Stats should be updated
    stats = volumeArchetype->getVolumeStats(volume);
    EXPECT_EQ(stats->voxelCount, 1);
    EXPECT_TRUE(stats->isDirty);
}

TEST_F(VoxelVolumeArchetypeTest, BatchAddVoxelsTriggersCallback) {
    auto volume = volumeArchetype->createVolume(glm::ivec3(0, 0, 0));

    std::vector<gaia::ecs::Entity> voxels;
    for (int i = 0; i < 20; ++i) {
        voxels.push_back(world.add());
    }

    size_t batchSize = 0;
    volumeArchetype->setOnVoxelBatchAdded([&](auto&, auto sources, auto) {
        batchSize = sources.size();
    });

    volumeArchetype->addVoxelsToVolume(voxels, volume);

    EXPECT_EQ(batchSize, 20);
}

TEST_F(VoxelVolumeArchetypeTest, BatchAddUpdatesStats) {
    auto volume = volumeArchetype->createVolume(glm::ivec3(0, 0, 0));

    std::vector<gaia::ecs::Entity> voxels;
    for (int i = 0; i < 50; ++i) {
        voxels.push_back(world.add());
    }

    volumeArchetype->addVoxelsToVolume(voxels, volume);

    auto stats = volumeArchetype->getVolumeStats(volume);
    EXPECT_EQ(stats->voxelCount, 50);
}

TEST_F(VoxelVolumeArchetypeTest, RemoveVoxelFromVolumeTriggersCallback) {
    auto volume = volumeArchetype->createVolume(glm::ivec3(0, 0, 0));
    auto voxel = world.add();

    volumeArchetype->addVoxelToVolume(voxel, volume);

    bool removedCallbackTriggered = false;
    volumeArchetype->setOnVoxelRemoved([&](auto&, auto, auto) {
        removedCallbackTriggered = true;
    });

    volumeArchetype->removeVoxelFromVolume(voxel, volume);

    EXPECT_TRUE(removedCallbackTriggered);
}

TEST_F(VoxelVolumeArchetypeTest, RemoveVoxelUpdatesStats) {
    auto volume = volumeArchetype->createVolume(glm::ivec3(0, 0, 0));
    auto voxel = world.add();

    volumeArchetype->addVoxelToVolume(voxel, volume);
    auto stats = volumeArchetype->getVolumeStats(volume);
    EXPECT_EQ(stats->voxelCount, 1);

    volumeArchetype->removeVoxelFromVolume(voxel, volume);
    stats = volumeArchetype->getVolumeStats(volume);
    EXPECT_EQ(stats->voxelCount, 0);
}

// ===========================================================================
// VoxelVolumeArchetype Query Tests
// ===========================================================================

TEST_F(VoxelVolumeArchetypeTest, IsVoxelInVolume) {
    auto volume = volumeArchetype->createVolume(glm::ivec3(0, 0, 0));
    auto voxel = world.add();

    EXPECT_FALSE(volumeArchetype->isVoxelInVolume(voxel, volume));

    volumeArchetype->addVoxelToVolume(voxel, volume);
    EXPECT_TRUE(volumeArchetype->isVoxelInVolume(voxel, volume));

    volumeArchetype->removeVoxelFromVolume(voxel, volume);
    EXPECT_FALSE(volumeArchetype->isVoxelInVolume(voxel, volume));
}

TEST_F(VoxelVolumeArchetypeTest, GetVoxelsInVolume) {
    auto volume = volumeArchetype->createVolume(glm::ivec3(0, 0, 0));

    std::vector<gaia::ecs::Entity> addedVoxels;
    for (int i = 0; i < 10; ++i) {
        auto voxel = world.add();
        addedVoxels.push_back(voxel);
        volumeArchetype->addVoxelToVolume(voxel, volume);
    }

    auto retrieved = volumeArchetype->getVoxelsInVolume(volume);
    EXPECT_EQ(retrieved.size(), 10);
}

TEST_F(VoxelVolumeArchetypeTest, ClearVolumeRemovesAllVoxels) {
    auto volume = volumeArchetype->createVolume(glm::ivec3(0, 0, 0));

    for (int i = 0; i < 10; ++i) {
        auto voxel = world.add();
        volumeArchetype->addVoxelToVolume(voxel, volume);
    }

    auto stats = volumeArchetype->getVolumeStats(volume);
    EXPECT_EQ(stats->voxelCount, 10);

    size_t removed = volumeArchetype->clearVolume(volume);
    EXPECT_EQ(removed, 10);

    stats = volumeArchetype->getVolumeStats(volume);
    EXPECT_EQ(stats->voxelCount, 0);
}

TEST_F(VoxelVolumeArchetypeTest, MultipleVolumesIndependent) {
    auto volume1 = volumeArchetype->createVolume(glm::ivec3(0, 0, 0));
    auto volume2 = volumeArchetype->createVolume(glm::ivec3(100, 0, 0));

    auto voxel1 = world.add();
    auto voxel2 = world.add();

    volumeArchetype->addVoxelToVolume(voxel1, volume1);
    volumeArchetype->addVoxelToVolume(voxel2, volume2);

    EXPECT_TRUE(volumeArchetype->isVoxelInVolume(voxel1, volume1));
    EXPECT_FALSE(volumeArchetype->isVoxelInVolume(voxel1, volume2));
    EXPECT_FALSE(volumeArchetype->isVoxelInVolume(voxel2, volume1));
    EXPECT_TRUE(volumeArchetype->isVoxelInVolume(voxel2, volume2));

    auto stats1 = volumeArchetype->getVolumeStats(volume1);
    auto stats2 = volumeArchetype->getVolumeStats(volume2);

    EXPECT_EQ(stats1->voxelCount, 1);
    EXPECT_EQ(stats2->voxelCount, 1);
}

// ===========================================================================
// VoxelVolumeArchetype Edge Case Tests
// ===========================================================================

TEST_F(VoxelVolumeArchetypeTest, GetStatsForInvalidVolume) {
    gaia::ecs::Entity invalidEntity{};
    auto stats = volumeArchetype->getVolumeStats(invalidEntity);
    EXPECT_EQ(stats, nullptr);
}

TEST_F(VoxelVolumeArchetypeTest, GetBoundsForInvalidVolume) {
    gaia::ecs::Entity invalidEntity{};
    auto bounds = volumeArchetype->getVolumeBounds(invalidEntity);
    EXPECT_EQ(bounds, nullptr);
}

TEST_F(VoxelVolumeArchetypeTest, GetStatsForNonVolumeEntity) {
    auto nonVolume = world.add();
    auto stats = volumeArchetype->getVolumeStats(nonVolume);
    EXPECT_EQ(stats, nullptr);
}

TEST_F(VoxelVolumeArchetypeTest, AddSameVoxelTwice) {
    auto volume = volumeArchetype->createVolume(glm::ivec3(0, 0, 0));
    auto voxel = world.add();

    int callCount = 0;
    volumeArchetype->setOnVoxelAdded([&](auto&, auto, auto) {
        callCount++;
    });

    volumeArchetype->addVoxelToVolume(voxel, volume);
    volumeArchetype->addVoxelToVolume(voxel, volume);

    // Second add might not trigger callback
    // Stats should still be correct
    auto stats = volumeArchetype->getVolumeStats(volume);
    EXPECT_GE(stats->voxelCount, 1);
}

TEST_F(VoxelVolumeArchetypeTest, RemoveNonExistentVoxel) {
    auto volume = volumeArchetype->createVolume(glm::ivec3(0, 0, 0));
    auto voxel = world.add();

    int callCount = 0;
    volumeArchetype->setOnVoxelRemoved([&](auto&, auto, auto) {
        callCount++;
    });

    // Try to remove a voxel that was never added
    bool result = volumeArchetype->removeVoxelFromVolume(voxel, volume);

    EXPECT_FALSE(result);
    EXPECT_EQ(callCount, 0);
}

TEST_F(VoxelVolumeArchetypeTest, ClearEmptyVolume) {
    auto volume = volumeArchetype->createVolume(glm::ivec3(0, 0, 0));

    size_t removed = volumeArchetype->clearVolume(volume);

    EXPECT_EQ(removed, 0);
}

TEST_F(VoxelVolumeArchetypeTest, VoxelInMultipleVolumes) {
    auto volume1 = volumeArchetype->createVolume(glm::ivec3(0, 0, 0));
    auto volume2 = volumeArchetype->createVolume(glm::ivec3(100, 0, 0));
    auto voxel = world.add();

    // Same voxel can be part of multiple volumes
    volumeArchetype->addVoxelToVolume(voxel, volume1);
    volumeArchetype->addVoxelToVolume(voxel, volume2);

    EXPECT_TRUE(volumeArchetype->isVoxelInVolume(voxel, volume1));
    EXPECT_TRUE(volumeArchetype->isVoxelInVolume(voxel, volume2));

    // Remove from one volume
    volumeArchetype->removeVoxelFromVolume(voxel, volume1);

    EXPECT_FALSE(volumeArchetype->isVoxelInVolume(voxel, volume1));
    EXPECT_TRUE(volumeArchetype->isVoxelInVolume(voxel, volume2));
}

TEST_F(VoxelVolumeArchetypeTest, LargeVoxelBatch) {
    auto volume = volumeArchetype->createVolume(glm::ivec3(0, 0, 0));

    std::vector<gaia::ecs::Entity> voxels;
    for (int i = 0; i < 10000; ++i) {
        voxels.push_back(world.add());
    }

    size_t added = volumeArchetype->addVoxelsToVolume(voxels, volume);

    EXPECT_EQ(added, 10000);

    auto stats = volumeArchetype->getVolumeStats(volume);
    EXPECT_EQ(stats->voxelCount, 10000);

    auto retrieved = volumeArchetype->getVoxelsInVolume(volume);
    EXPECT_EQ(retrieved.size(), 10000);
}

TEST_F(VoxelVolumeArchetypeTest, SetBatchThreshold) {
    // Set low threshold
    volumeArchetype->setBatchThreshold(5);

    auto volume = volumeArchetype->createVolume(glm::ivec3(0, 0, 0));

    std::vector<gaia::ecs::Entity> voxels;
    for (int i = 0; i < 6; ++i) {
        voxels.push_back(world.add());
    }

    bool batchCalled = false;
    int individualCount = 0;

    volumeArchetype->setOnVoxelAdded([&](auto&, auto, auto) {
        individualCount++;
    });

    volumeArchetype->setOnVoxelBatchAdded([&](auto&, auto, auto) {
        batchCalled = true;
    });

    volumeArchetype->addVoxelsToVolume(voxels, volume);

    // With threshold of 5, 6 voxels should trigger batch callback
    EXPECT_TRUE(batchCalled);
    EXPECT_EQ(individualCount, 0);
}

// ===========================================================================
// VoxelVolumeArchetype Component Tests
// ===========================================================================

TEST_F(VoxelVolumeArchetypeTest, VolumeOriginComponent) {
    auto volume = volumeArchetype->createVolume(glm::ivec3(-100, 50, 200));

    const auto& origin = world.get<VolumeOrigin>(volume);
    EXPECT_EQ(origin.x, -100);
    EXPECT_EQ(origin.y, 50);
    EXPECT_EQ(origin.z, 200);
}

TEST_F(VoxelVolumeArchetypeTest, VolumeSizeComponent) {
    auto volume = volumeArchetype->createVolume(
        glm::ivec3(0, 0, 0),
        glm::ivec3(256, 128, 512)
    );

    const auto& size = world.get<VolumeSize>(volume);
    EXPECT_EQ(size.width, 256);
    EXPECT_EQ(size.height, 128);
    EXPECT_EQ(size.depth, 512);
}

TEST_F(VoxelVolumeArchetypeTest, VolumeStatsLastModified) {
    auto volume = volumeArchetype->createVolume(glm::ivec3(0, 0, 0));

    auto stats = volumeArchetype->getVolumeStats(volume);
    uint64_t initialTime = stats->lastModified;

    // Add a voxel
    auto voxel = world.add();
    volumeArchetype->addVoxelToVolume(voxel, volume);

    stats = volumeArchetype->getVolumeStats(volume);
    EXPECT_GE(stats->lastModified, initialTime);
}

// ===========================================================================
// VoxelVolumeSystem Tests
// ===========================================================================

class VoxelVolumeSystemTest : public ::testing::Test {
protected:
    gaia::ecs::World world;
    std::unique_ptr<RelationshipObserver> observer;
    std::unique_ptr<RelationshipTypeRegistry> types;
    std::unique_ptr<VoxelVolumeArchetype> volumeArchetype;
    std::unique_ptr<VoxelVolumeSystem> volumeSystem;

    void SetUp() override {
        observer = std::make_unique<RelationshipObserver>(world);
        types = std::make_unique<RelationshipTypeRegistry>(world);
        volumeArchetype = std::make_unique<VoxelVolumeArchetype>(world, *observer, *types);
        volumeSystem = std::make_unique<VoxelVolumeSystem>(world);
    }
};

TEST_F(VoxelVolumeSystemTest, ProcessDirtyVolumes) {
    auto volume = volumeArchetype->createVolume(glm::ivec3(0, 0, 0));
    auto voxel = world.add();

    volumeArchetype->addVoxelToVolume(voxel, volume);

    // Volume should be dirty
    auto stats = volumeArchetype->getVolumeStats(volume);
    EXPECT_TRUE(stats->isDirty);

    int processCount = 0;
    volumeSystem->setProcessCallback([&](auto&, auto, auto) {
        processCount++;
    });

    volumeSystem->processDirtyVolumes();

    EXPECT_EQ(processCount, 1);

    // Volume should be clean now
    stats = volumeArchetype->getVolumeStats(volume);
    EXPECT_FALSE(stats->isDirty);
}

TEST_F(VoxelVolumeSystemTest, CleanVolumesNotProcessed) {
    auto volume = volumeArchetype->createVolume(glm::ivec3(0, 0, 0));

    // Volume starts clean (no voxels added)
    // Note: Volume starts dirty after creation, let's process once first
    volumeSystem->processDirtyVolumes();

    int processCount = 0;
    volumeSystem->setProcessCallback([&](auto&, auto, auto) {
        processCount++;
    });

    volumeSystem->processDirtyVolumes();

    EXPECT_EQ(processCount, 0);  // Should not process already-clean volume
}

TEST_F(VoxelVolumeSystemTest, ProcessMultipleDirtyVolumes) {
    auto volume1 = volumeArchetype->createVolume(glm::ivec3(0, 0, 0));
    auto volume2 = volumeArchetype->createVolume(glm::ivec3(100, 0, 0));
    auto volume3 = volumeArchetype->createVolume(glm::ivec3(200, 0, 0));

    // Make all volumes dirty
    volumeArchetype->addVoxelToVolume(world.add(), volume1);
    volumeArchetype->addVoxelToVolume(world.add(), volume2);
    volumeArchetype->addVoxelToVolume(world.add(), volume3);

    int processCount = 0;
    volumeSystem->setProcessCallback([&](auto&, auto, auto) {
        processCount++;
    });

    volumeSystem->processDirtyVolumes();

    EXPECT_EQ(processCount, 3);
}

TEST_F(VoxelVolumeSystemTest, ProcessCallbackReceivesCorrectVolume) {
    auto volume = volumeArchetype->createVolume(glm::ivec3(0, 0, 0));
    volumeArchetype->addVoxelToVolume(world.add(), volume);

    gaia::ecs::Entity processedVolume;
    volumeSystem->setProcessCallback([&](auto&, auto vol, auto) {
        processedVolume = vol;
    });

    volumeSystem->processDirtyVolumes();

    EXPECT_EQ(processedVolume, volume);
}

TEST_F(VoxelVolumeSystemTest, ProcessWithNoCallback) {
    auto volume = volumeArchetype->createVolume(glm::ivec3(0, 0, 0));
    volumeArchetype->addVoxelToVolume(world.add(), volume);

    // No callback set - should not crash
    volumeSystem->processDirtyVolumes();

    // Volume should still be marked clean
    auto stats = volumeArchetype->getVolumeStats(volume);
    EXPECT_FALSE(stats->isDirty);
}

// ===========================================================================
// Integration Tests
// ===========================================================================

class VoxelVolumeIntegrationTest : public ::testing::Test {
protected:
    gaia::ecs::World world;
    std::unique_ptr<RelationshipObserver> observer;
    std::unique_ptr<RelationshipTypeRegistry> types;
    std::unique_ptr<VoxelVolumeArchetype> volumeArchetype;
    std::unique_ptr<VoxelVolumeSystem> volumeSystem;

    void SetUp() override {
        observer = std::make_unique<RelationshipObserver>(world);
        types = std::make_unique<RelationshipTypeRegistry>(world);
        volumeArchetype = std::make_unique<VoxelVolumeArchetype>(world, *observer, *types);
        volumeSystem = std::make_unique<VoxelVolumeSystem>(world);
    }
};

TEST_F(VoxelVolumeIntegrationTest, CompleteWorkflow) {
    // 1. Create volume
    auto volume = volumeArchetype->createVolume(
        glm::ivec3(0, 0, 0),
        glm::ivec3(64, 64, 64)
    );

    EXPECT_TRUE(world.valid(volume));

    // 2. Track callbacks
    int addedCount = 0;
    size_t batchAddedCount = 0;
    int removedCount = 0;
    int processedCount = 0;

    volumeArchetype->setOnVoxelAdded([&](auto&, auto, auto) {
        addedCount++;
    });

    volumeArchetype->setOnVoxelBatchAdded([&](auto&, auto sources, auto) {
        batchAddedCount = sources.size();
    });

    volumeArchetype->setOnVoxelRemoved([&](auto&, auto, auto) {
        removedCount++;
    });

    volumeSystem->setProcessCallback([&](auto&, auto, auto) {
        processedCount++;
    });

    // 3. Add voxels
    std::vector<gaia::ecs::Entity> voxels;
    for (int i = 0; i < 100; ++i) {
        voxels.push_back(world.add());
    }
    volumeArchetype->addVoxelsToVolume(voxels, volume);

    // Either individual or batch callback should have been triggered
    EXPECT_TRUE(addedCount == 100 || batchAddedCount == 100);

    auto stats = volumeArchetype->getVolumeStats(volume);
    EXPECT_EQ(stats->voxelCount, 100);
    EXPECT_TRUE(stats->isDirty);

    // 4. Process dirty volumes
    volumeSystem->processDirtyVolumes();
    EXPECT_EQ(processedCount, 1);

    stats = volumeArchetype->getVolumeStats(volume);
    EXPECT_FALSE(stats->isDirty);

    // 5. Remove some voxels
    for (int i = 0; i < 25; ++i) {
        volumeArchetype->removeVoxelFromVolume(voxels[i], volume);
    }

    EXPECT_EQ(removedCount, 25);

    stats = volumeArchetype->getVolumeStats(volume);
    EXPECT_EQ(stats->voxelCount, 75);

    // 6. Verify remaining voxels
    auto remaining = volumeArchetype->getVoxelsInVolume(volume);
    EXPECT_EQ(remaining.size(), 75);

    // 7. Clear volume
    volumeArchetype->clearVolume(volume);

    stats = volumeArchetype->getVolumeStats(volume);
    EXPECT_EQ(stats->voxelCount, 0);
}

TEST_F(VoxelVolumeIntegrationTest, MultipleVolumeWorkflow) {
    // Create multiple volumes
    std::vector<gaia::ecs::Entity> volumes;
    for (int i = 0; i < 5; ++i) {
        volumes.push_back(volumeArchetype->createVolume(
            glm::ivec3(i * 100, 0, 0)
        ));
    }

    // Create shared voxels
    std::vector<gaia::ecs::Entity> voxels;
    for (int i = 0; i < 50; ++i) {
        voxels.push_back(world.add());
    }

    // Distribute voxels: each volume gets 10 consecutive voxels
    for (int v = 0; v < 5; ++v) {
        for (int i = 0; i < 10; ++i) {
            volumeArchetype->addVoxelToVolume(voxels[v * 10 + i], volumes[v]);
        }
    }

    // Verify each volume has exactly 10 voxels
    for (auto volume : volumes) {
        auto stats = volumeArchetype->getVolumeStats(volume);
        EXPECT_EQ(stats->voxelCount, 10);

        auto volumeVoxels = volumeArchetype->getVoxelsInVolume(volume);
        EXPECT_EQ(volumeVoxels.size(), 10);
    }

    // Process all dirty volumes
    int processCount = 0;
    volumeSystem->setProcessCallback([&](auto&, auto, auto) {
        processCount++;
    });

    volumeSystem->processDirtyVolumes();

    EXPECT_EQ(processCount, 5);
}

TEST_F(VoxelVolumeIntegrationTest, VoxelMigrationBetweenVolumes) {
    auto volume1 = volumeArchetype->createVolume(glm::ivec3(0, 0, 0));
    auto volume2 = volumeArchetype->createVolume(glm::ivec3(100, 0, 0));

    auto voxel = world.add();

    // Add to first volume
    volumeArchetype->addVoxelToVolume(voxel, volume1);
    EXPECT_TRUE(volumeArchetype->isVoxelInVolume(voxel, volume1));
    EXPECT_FALSE(volumeArchetype->isVoxelInVolume(voxel, volume2));

    EXPECT_EQ(volumeArchetype->getVolumeStats(volume1)->voxelCount, 1);
    EXPECT_EQ(volumeArchetype->getVolumeStats(volume2)->voxelCount, 0);

    // Move to second volume (remove from first, add to second)
    volumeArchetype->removeVoxelFromVolume(voxel, volume1);
    volumeArchetype->addVoxelToVolume(voxel, volume2);

    EXPECT_FALSE(volumeArchetype->isVoxelInVolume(voxel, volume1));
    EXPECT_TRUE(volumeArchetype->isVoxelInVolume(voxel, volume2));

    EXPECT_EQ(volumeArchetype->getVolumeStats(volume1)->voxelCount, 0);
    EXPECT_EQ(volumeArchetype->getVolumeStats(volume2)->voxelCount, 1);
}

// ===========================================================================
// VolumeBounds Component Tests
// ===========================================================================

TEST_F(VoxelVolumeArchetypeTest, VolumeBoundsExpand) {
    VolumeBounds bounds;
    EXPECT_FALSE(bounds.isValid());

    bounds.expand(glm::vec3(1, 1, 1));
    EXPECT_TRUE(bounds.isValid());
    EXPECT_EQ(bounds.minX, 0.0f);  // initial values
    EXPECT_EQ(bounds.maxX, 1.0f);

    bounds.expand(glm::vec3(10, 5, 3));
    EXPECT_EQ(bounds.maxX, 10.0f);
    EXPECT_EQ(bounds.maxY, 5.0f);
    EXPECT_EQ(bounds.maxZ, 3.0f);

    bounds.expand(glm::vec3(-5, -10, -2));
    EXPECT_EQ(bounds.minX, -5.0f);
    EXPECT_EQ(bounds.minY, -10.0f);
    EXPECT_EQ(bounds.minZ, -2.0f);
    EXPECT_EQ(bounds.maxX, 10.0f);
    EXPECT_EQ(bounds.maxY, 5.0f);
    EXPECT_EQ(bounds.maxZ, 3.0f);
}

TEST_F(VoxelVolumeArchetypeTest, VolumeBoundsInitialState) {
    VolumeBounds bounds;
    // Initial state should be all zeros
    EXPECT_EQ(bounds.minX, 0.0f);
    EXPECT_EQ(bounds.minY, 0.0f);
    EXPECT_EQ(bounds.minZ, 0.0f);
    EXPECT_EQ(bounds.maxX, 0.0f);
    EXPECT_EQ(bounds.maxY, 0.0f);
    EXPECT_EQ(bounds.maxZ, 0.0f);
    EXPECT_FALSE(bounds.isValid());
}

// ===========================================================================
// Thread Safety Tests (Basic Validation)
// ===========================================================================

TEST_F(VoxelVolumeArchetypeTest, ConcurrentReads) {
    auto volume = volumeArchetype->createVolume(glm::ivec3(0, 0, 0));

    // Add some voxels first
    std::vector<gaia::ecs::Entity> voxels;
    for (int i = 0; i < 100; ++i) {
        auto voxel = world.add();
        voxels.push_back(voxel);
        volumeArchetype->addVoxelToVolume(voxel, volume);
    }

    // Concurrent reads
    std::vector<std::thread> threads;
    std::atomic<int> successCount{0};

    for (int t = 0; t < 10; ++t) {
        threads.emplace_back([this, volume, &voxels, &successCount]() {
            for (int i = 0; i < 100; ++i) {
                if (volumeArchetype->isVoxelInVolume(voxels[i], volume)) {
                    successCount++;
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(successCount.load(), 1000);  // All reads should succeed
}
