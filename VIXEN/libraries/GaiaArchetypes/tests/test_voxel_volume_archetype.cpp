#include <gtest/gtest.h>
#include "VoxelVolumeArchetype.h"
#include <gaia.h>

using namespace GaiaArchetype;

// ============================================================================
// VoxelVolumeArchetype Tests
// ============================================================================

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

// ============================================================================
// VoxelVolumeSystem Tests
// ============================================================================

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
