#include <gtest/gtest.h>
#include "GaiaVoxelWorld.h"
#include "ComponentData.h"
#include <glm/glm.hpp>
#include <thread>
#include <algorithm>

using namespace GaiaVoxel;

// ===========================================================================
// VoxelCreationRequest API Tests (New - Session 6)
// ===========================================================================

TEST(GaiaVoxelWorldCoverageTest, CreateVoxelWithRequest_MinimalComponents) {
    GaiaVoxelWorld world;

    ComponentQueryRequest components[] = {
        Density{0.5f}
    };

    VoxelCreationRequest request{glm::vec3(1.0f, 2.0f, 3.0f), components};
    auto entity = world.createVoxel(request);

    ASSERT_TRUE(world.exists(entity));

    // Check density was set
    auto density = world.getDensity(entity);
    ASSERT_TRUE(density.has_value());
    EXPECT_FLOAT_EQ(density.value(), 0.5f);

    // Check position (always set via MortonKey)
    auto pos = world.getPosition(entity);
    ASSERT_TRUE(pos.has_value());
    EXPECT_EQ(pos.value(), glm::vec3(1.0f, 2.0f, 3.0f));
}

TEST(GaiaVoxelWorldCoverageTest, CreateVoxelWithRequest_AllComponents) {
    GaiaVoxelWorld world;

    ComponentQueryRequest components[] = {
        Density{1.0f},
        Color{glm::vec3(1.0f, 0.0f, 0.0f)},
        Normal{glm::vec3(0.0f, 1.0f, 0.0f)},
        Material{42},
        EmissionIntensity{0.8f},
        Emission{glm::vec3(1.0f, 0.5f, 0.0f)}
    };

    VoxelCreationRequest request{glm::vec3(5.0f, 10.0f, 15.0f), components};
    auto entity = world.createVoxel(request);

    ASSERT_TRUE(world.exists(entity));

    // Verify all components
    EXPECT_FLOAT_EQ(world.getDensity(entity).value(), 1.0f);
    EXPECT_EQ(world.getColor(entity).value(), glm::vec3(1.0f, 0.0f, 0.0f));
    EXPECT_EQ(world.getNormal(entity).value(), glm::vec3(0.0f, 1.0f, 0.0f));
}

TEST(GaiaVoxelWorldCoverageTest, CreateVoxelsBatch_EmptyBatch) {
    GaiaVoxelWorld world;

    std::span<const VoxelCreationRequest> emptyBatch;
    auto entities = world.createVoxelsBatch(emptyBatch);

    EXPECT_TRUE(entities.empty());
}

TEST(GaiaVoxelWorldCoverageTest, CreateVoxelsBatch_MixedComponents) {
    GaiaVoxelWorld world;

    // Voxel 1: Density + Color
    ComponentQueryRequest comps1[] = {
        Density{1.0f},
        Color{glm::vec3(1, 0, 0)}
    };

    // Voxel 2: Only Density
    ComponentQueryRequest comps2[] = {
        Density{0.5f}
    };

    // Voxel 3: Density + Color + Normal
    ComponentQueryRequest comps3[] = {
        Density{0.8f},
        Color{glm::vec3(0, 1, 0)},
        Normal{glm::vec3(0, 0, 1)}
    };

    VoxelCreationRequest requests[] = {
        {glm::vec3(0, 0, 0), comps1},
        {glm::vec3(1, 0, 0), comps2},
        {glm::vec3(2, 0, 0), comps3}
    };

    auto entities = world.createVoxelsBatch(requests);

    ASSERT_EQ(entities.size(), 3);
    EXPECT_TRUE(world.exists(entities[0]));
    EXPECT_TRUE(world.exists(entities[1]));
    EXPECT_TRUE(world.exists(entities[2]));

    // Verify component presence
    EXPECT_TRUE(world.hasComponent<Color>(entities[0]));
    EXPECT_FALSE(world.hasComponent<Color>(entities[1]));  // No color
    EXPECT_TRUE(world.hasComponent<Normal>(entities[2]));
    EXPECT_FALSE(world.hasComponent<Normal>(entities[0])); // No normal
}

// ===========================================================================
// Chunk Operations Tests (New API)
// ===========================================================================

TEST(GaiaVoxelWorldCoverageTest, InsertChunk_SingleVoxel) {
    GaiaVoxelWorld world;

    ComponentQueryRequest comps[] = {Density{1.0f}};
    VoxelCreationRequest voxels[] = {
        {glm::vec3(0, 0, 0), comps}
    };

    auto chunkEntity = world.insertChunk(glm::ivec3(0, 0, 0), voxels);

    ASSERT_TRUE(world.exists(chunkEntity));

    auto voxelsInChunk = world.getVoxelsInChunk(chunkEntity);
    ASSERT_EQ(voxelsInChunk.size(), 1);
    EXPECT_TRUE(world.exists(voxelsInChunk[0]));
}

TEST(GaiaVoxelWorldCoverageTest, InsertChunk_FullBrick8x8x8) {
    GaiaVoxelWorld world;

    // Create 512 voxels (8³)
    std::vector<VoxelCreationRequest> voxels;
    voxels.reserve(512);

    ComponentQueryRequest comps[] = {Density{1.0f}, Color{glm::vec3(0, 1, 0)}};

    for (int z = 0; z < 8; ++z) {
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                glm::vec3 pos(x * 0.1f, y * 0.1f, z * 0.1f);
                voxels.push_back({pos, comps});
            }
        }
    }

    auto chunkEntity = world.insertChunk(glm::ivec3(0, 0, 0), voxels);

    ASSERT_TRUE(world.exists(chunkEntity));

    auto voxelsInChunk = world.getVoxelsInChunk(chunkEntity);
    EXPECT_EQ(voxelsInChunk.size(), 512);

    // Verify all voxels exist
    for (auto voxel : voxelsInChunk) {
        EXPECT_TRUE(world.exists(voxel));
        EXPECT_TRUE(world.getDensity(voxel).has_value());
    }
}

TEST(GaiaVoxelWorldCoverageTest, InsertChunk_MultipleChunks) {
    GaiaVoxelWorld world;

    ComponentQueryRequest comps[] = {Density{1.0f}};

    // Chunk 1 at (0, 0, 0)
    VoxelCreationRequest voxels1[] = {
        {glm::vec3(0, 0, 0), comps},
        {glm::vec3(0.1f, 0, 0), comps}
    };
    auto chunk1 = world.insertChunk(glm::ivec3(0, 0, 0), voxels1);

    // Chunk 2 at (8, 0, 0)
    VoxelCreationRequest voxels2[] = {
        {glm::vec3(8, 0, 0), comps},
        {glm::vec3(8.1f, 0, 0), comps}
    };
    auto chunk2 = world.insertChunk(glm::ivec3(8, 0, 0), voxels2);

    EXPECT_NE(chunk1, chunk2);
    EXPECT_TRUE(world.exists(chunk1));
    EXPECT_TRUE(world.exists(chunk2));

    auto voxels1InChunk = world.getVoxelsInChunk(chunk1);
    auto voxels2InChunk = world.getVoxelsInChunk(chunk2);

    EXPECT_EQ(voxels1InChunk.size(), 2);
    EXPECT_EQ(voxels2InChunk.size(), 2);
}

TEST(GaiaVoxelWorldCoverageTest, FindChunkByOrigin_Exists) {
    GaiaVoxelWorld world;

    ComponentQueryRequest comps[] = {Density{1.0f}};
    VoxelCreationRequest voxels[] = {{glm::vec3(0, 0, 0), comps}};

    auto chunkEntity = world.insertChunk(glm::ivec3(0, 0, 0), voxels);

    auto foundChunk = world.findChunkByOrigin(glm::ivec3(0, 0, 0));
    ASSERT_TRUE(foundChunk.has_value());
    EXPECT_EQ(foundChunk.value(), chunkEntity);
}

TEST(GaiaVoxelWorldCoverageTest, FindChunkByOrigin_NotFound) {
    GaiaVoxelWorld world;

    auto foundChunk = world.findChunkByOrigin(glm::ivec3(0, 0, 0));
    EXPECT_FALSE(foundChunk.has_value());
}

// ===========================================================================
// Component Existence Tests (Template API)
// ===========================================================================

TEST(GaiaVoxelWorldCoverageTest, HasComponent_TemplateAPI) {
    GaiaVoxelWorld world;

    ComponentQueryRequest comps[] = {
        Density{1.0f},
        Color{glm::vec3(1, 0, 0)}
    };
    VoxelCreationRequest request{glm::vec3(0, 0, 0), comps};
    auto entity = world.createVoxel(request);

    // Test template API
    EXPECT_TRUE(world.hasComponent<Density>(entity));
    EXPECT_TRUE(world.hasComponent<Color>(entity));
    EXPECT_FALSE(world.hasComponent<Normal>(entity));
    EXPECT_FALSE(world.hasComponent<Material>(entity));
}

TEST(GaiaVoxelWorldCoverageTest, HasComponent_StringAPI) {
    GaiaVoxelWorld world;

    ComponentQueryRequest comps[] = {
        Density{1.0f},
        Normal{glm::vec3(0, 1, 0)}
    };
    VoxelCreationRequest request{glm::vec3(0, 0, 0), comps};
    auto entity = world.createVoxel(request);

    // Test string-based API
    EXPECT_TRUE(world.hasComponent(entity, "density"));
    EXPECT_TRUE(world.hasComponent(entity, "normal"));
    EXPECT_FALSE(world.hasComponent(entity, "color"));
    EXPECT_FALSE(world.hasComponent(entity, "material"));
}

// ===========================================================================
// Query Tests (Additional Coverage)
// ===========================================================================

TEST(GaiaVoxelWorldCoverageTest, QueryBrick_Empty) {
    GaiaVoxelWorld world;

    auto voxels = world.queryBrick(glm::ivec3(0, 0, 0), 8);
    EXPECT_TRUE(voxels.empty());
}

TEST(GaiaVoxelWorldCoverageTest, QueryBrick_WithVoxels) {
    GaiaVoxelWorld world;

    // Create voxels in brick coordinate (0, 0, 0)
    for (int i = 0; i < 10; ++i) {
        world.createVoxel(glm::vec3(i * 0.1f, 0, 0), 1.0f);
    }

    auto voxels = world.queryBrick(glm::ivec3(0, 0, 0), 8);
    EXPECT_EQ(voxels.size(), 10);
}

TEST(GaiaVoxelWorldCoverageTest, CountVoxelsInRegion_Empty) {
    GaiaVoxelWorld world;

    size_t count = world.countVoxelsInRegion(glm::vec3(0, 0, 0), glm::vec3(10, 10, 10));
    EXPECT_EQ(count, 0);
}

TEST(GaiaVoxelWorldCoverageTest, CountVoxelsInRegion_Matches) {
    GaiaVoxelWorld world;

    // Create 5 voxels in region [0, 5]
    for (int i = 0; i < 5; ++i) {
        world.createVoxel(glm::vec3(i, 0, 0), 1.0f);
    }

    size_t count = world.countVoxelsInRegion(glm::vec3(-1, -1, -1), glm::vec3(10, 10, 10));
    EXPECT_EQ(count, 5);

    // Count should match queryRegion size
    auto voxels = world.queryRegion(glm::vec3(-1, -1, -1), glm::vec3(10, 10, 10));
    EXPECT_EQ(count, voxels.size());
}

// ===========================================================================
// Edge Cases & Error Handling
// ===========================================================================

TEST(GaiaVoxelWorldCoverageTest, DestroyNonExistentVoxel_NoThrow) {
    GaiaVoxelWorld world;

    gaia::ecs::Entity fakeEntity;  // Invalid entity
    EXPECT_NO_THROW(world.destroyVoxel(fakeEntity));
}

TEST(GaiaVoxelWorldCoverageTest, GetComponentFromDestroyedVoxel) {
    GaiaVoxelWorld world;

    auto entity = world.createVoxel(glm::vec3(0, 0, 0), 1.0f);
    ASSERT_TRUE(world.exists(entity));

    world.destroyVoxel(entity);
    EXPECT_FALSE(world.exists(entity));

    // Getters should return nullopt for destroyed entity
    EXPECT_FALSE(world.getDensity(entity).has_value());
    EXPECT_FALSE(world.getColor(entity).has_value());
    EXPECT_FALSE(world.getNormal(entity).has_value());
}

TEST(GaiaVoxelWorldCoverageTest, SetComponentOnNonExistentVoxel_NoThrow) {
    GaiaVoxelWorld world;

    gaia::ecs::Entity fakeEntity;  // Invalid entity

    // Should not throw (implementation may silently fail or handle gracefully)
    EXPECT_NO_THROW(world.setDensity(fakeEntity, 1.0f));
    EXPECT_NO_THROW(world.setColor(fakeEntity, glm::vec3(1, 0, 0)));
    EXPECT_NO_THROW(world.setNormal(fakeEntity, glm::vec3(0, 1, 0)));
}

// ===========================================================================
// Performance & Stress Tests
// ===========================================================================

TEST(GaiaVoxelWorldCoverageTest, CreateAndDestroy10kVoxels) {
    GaiaVoxelWorld world;

    std::vector<GaiaVoxelWorld::EntityID> entities;
    entities.reserve(10000);

    // Create 10k voxels
    for (int i = 0; i < 10000; ++i) {
        auto entity = world.createVoxel(glm::vec3(i * 0.1f, 0, 0), 1.0f);
        entities.push_back(entity);
    }

    EXPECT_EQ(entities.size(), 10000);

    // Verify all exist
    for (auto entity : entities) {
        EXPECT_TRUE(world.exists(entity));
    }

    // Destroy all
    for (auto entity : entities) {
        world.destroyVoxel(entity);
    }

    // Verify all destroyed
    for (auto entity : entities) {
        EXPECT_FALSE(world.exists(entity));
    }
}

TEST(GaiaVoxelWorldCoverageTest, BatchVsIndividualCreation_SameResult) {
    GaiaVoxelWorld world1;
    GaiaVoxelWorld world2;

    ComponentQueryRequest comps[] = {
        Density{1.0f},
        Color{glm::vec3(1, 0, 0)}
    };

    // Individual creation
    std::vector<GaiaVoxelWorld::EntityID> individual;
    for (int i = 0; i < 100; ++i) {
        VoxelCreationRequest req{glm::vec3(i, 0, 0), comps};
        individual.push_back(world1.createVoxel(req));
    }

    // Batch creation
    std::vector<VoxelCreationRequest> requests;
    for (int i = 0; i < 100; ++i) {
        requests.push_back({glm::vec3(i, 0, 0), comps});
    }
    auto batch = world2.createVoxelsBatch(requests);

    EXPECT_EQ(individual.size(), batch.size());
    EXPECT_EQ(individual.size(), 100);
}

// ===========================================================================
// Spatial Chunk Coherence Tests (Auto-parenting to nearby chunks)
// ===========================================================================

TEST(GaiaVoxelWorldCoverageTest, CreateVoxel_AutoParentToExistingChunk) {
    GaiaVoxelWorld world;

    // 1. Create chunk at origin (0, 0, 0) with 8 voxels (cbrt(8) = 2, bounds = [0, 2))
    ComponentQueryRequest comps[] = {Density{1.0f}, Color{glm::vec3(1, 0, 0)}};
    VoxelCreationRequest chunkVoxels[8] = {
        {glm::vec3(0, 0, 0), comps}, {glm::vec3(0.5f, 0, 0), comps},
        {glm::vec3(1, 0, 0), comps}, {glm::vec3(1.5f, 0, 0), comps},
        {glm::vec3(0, 0.5f, 0), comps}, {glm::vec3(0.5f, 0.5f, 0), comps},
        {glm::vec3(1, 0.5f, 0), comps}, {glm::vec3(1.5f, 0.5f, 0), comps}
    };
    auto chunkEntity = world.insertChunk(glm::ivec3(0, 0, 0), chunkVoxels);

    ASSERT_TRUE(world.exists(chunkEntity));

    // 2. Create individual voxel WITHIN chunk bounds [0, 2) (should auto-parent)
    VoxelCreationRequest individualVoxel{glm::vec3(1.0f, 1.0f, 0.5f), comps};
    auto voxelEntity = world.createVoxel(individualVoxel);

    ASSERT_TRUE(world.exists(voxelEntity));

    // 3. Verify voxel is parented to chunk
    auto voxelsInChunk = world.getVoxelsInChunk(chunkEntity);
    EXPECT_EQ(voxelsInChunk.size(), 9) << "Chunk should contain 8 original + 1 auto-parented voxel";

    // Verify the individual voxel is in the chunk
    bool found = std::find(voxelsInChunk.begin(), voxelsInChunk.end(), voxelEntity) != voxelsInChunk.end();
    EXPECT_TRUE(found) << "Individually created voxel should be auto-parented to existing chunk";
}

TEST(GaiaVoxelWorldCoverageTest, CreateVoxel_NoAutoParent_OutsideChunkBounds) {
    GaiaVoxelWorld world;

    // 1. Create chunk at origin (0, 0, 0)
    ComponentQueryRequest comps[] = {Density{1.0f}};
    VoxelCreationRequest chunkVoxel{glm::vec3(0, 0, 0), comps};
    auto chunkEntity = world.insertChunk(glm::ivec3(0, 0, 0), std::span(&chunkVoxel, 1));

    // 2. Create voxel OUTSIDE chunk bounds (should NOT auto-parent)
    VoxelCreationRequest outsideVoxel{glm::vec3(10.0f, 10.0f, 10.0f), comps};
    auto voxelEntity = world.createVoxel(outsideVoxel);

    // 3. Verify voxel is NOT in chunk
    auto voxelsInChunk = world.getVoxelsInChunk(chunkEntity);
    EXPECT_EQ(voxelsInChunk.size(), 1) << "Chunk should only contain original voxel";

    bool found = std::find(voxelsInChunk.begin(), voxelsInChunk.end(), voxelEntity) != voxelsInChunk.end();
    EXPECT_FALSE(found) << "Voxel outside chunk bounds should NOT be auto-parented";
}

TEST(GaiaVoxelWorldCoverageTest, CreateVoxel_AutoParent_MultipleChunks) {
    GaiaVoxelWorld world;

    ComponentQueryRequest comps[] = {Density{1.0f}};

    // Create two chunks at different origins with 8 voxels each (cbrt(8) = 2, span = 8)
    VoxelCreationRequest chunk1Init[8] = {
        {glm::vec3(0, 0, 0), comps}, {glm::vec3(0.5f, 0, 0), comps},
        {glm::vec3(1, 0, 0), comps}, {glm::vec3(1.5f, 0, 0), comps},
        {glm::vec3(0, 0.5f, 0), comps}, {glm::vec3(0.5f, 0.5f, 0), comps},
        {glm::vec3(1, 0.5f, 0), comps}, {glm::vec3(1.5f, 0.5f, 0), comps}
    };
    auto chunk1 = world.insertChunk(glm::ivec3(0, 0, 0), chunk1Init);

    VoxelCreationRequest chunk2Init[8] = {
        {glm::vec3(16, 0, 0), comps}, {glm::vec3(16.5f, 0, 0), comps},
        {glm::vec3(17, 0, 0), comps}, {glm::vec3(17.5f, 0, 0), comps},
        {glm::vec3(16, 0.5f, 0), comps}, {glm::vec3(16.5f, 0.5f, 0), comps},
        {glm::vec3(17, 0.5f, 0), comps}, {glm::vec3(17.5f, 0.5f, 0), comps}
    };
    auto chunk2 = world.insertChunk(glm::ivec3(16, 0, 0), chunk2Init);

    // Create voxel in chunk1's bounds [0, 2) per axis
    VoxelCreationRequest inChunk1{glm::vec3(0.25f, 0.25f, 0.25f), comps};
    auto voxel1Entity = world.createVoxel(inChunk1);

    // Create voxel in chunk2's bounds [16, 18) per axis
    VoxelCreationRequest inChunk2{glm::vec3(16.25f, 0.25f, 0.25f), comps};
    auto voxel2Entity = world.createVoxel(inChunk2);

    // Verify correct parenting
    auto chunk1Voxels = world.getVoxelsInChunk(chunk1);
    auto chunk2Voxels = world.getVoxelsInChunk(chunk2);

    EXPECT_EQ(chunk1Voxels.size(), 9); // 8 original + 1 auto-parented
    EXPECT_EQ(chunk2Voxels.size(), 9); // 8 original + 1 auto-parented

    EXPECT_TRUE(std::find(chunk1Voxels.begin(), chunk1Voxels.end(), voxel1Entity) != chunk1Voxels.end());
    EXPECT_TRUE(std::find(chunk2Voxels.begin(), chunk2Voxels.end(), voxel2Entity) != chunk2Voxels.end());
}

TEST(GaiaVoxelWorldCoverageTest, CreateVoxelsBatch_AutoParent_ToExistingChunk) {
    GaiaVoxelWorld world;

    ComponentQueryRequest comps[] = {Density{1.0f}};

    // Create chunk at origin with 8 voxels (cbrt(8) = 2, bounds [0, 2), span = 8)
    VoxelCreationRequest chunkVoxels[8] = {
        {glm::vec3(0, 0, 0), comps}, {glm::vec3(0.5f, 0, 0), comps},
        {glm::vec3(1, 0, 0), comps}, {glm::vec3(1.5f, 0, 0), comps},
        {glm::vec3(0, 0.5f, 0), comps}, {glm::vec3(0.5f, 0.5f, 0), comps},
        {glm::vec3(1, 0.5f, 0), comps}, {glm::vec3(1.5f, 0.5f, 0), comps}
    };
    auto chunkEntity = world.insertChunk(glm::ivec3(0, 0, 0), chunkVoxels);

    // Create batch of voxels within chunk bounds [0, 2)
    VoxelCreationRequest batchVoxels[] = {
        {glm::vec3(0.25f, 0, 0), comps},
        {glm::vec3(0.75f, 0, 0), comps},
        {glm::vec3(1.25f, 0, 0), comps}
    };

    auto entities = world.createVoxelsBatch(batchVoxels);
    ASSERT_EQ(entities.size(), 3);

    // All batch voxels should be auto-parented to chunk
    auto voxelsInChunk = world.getVoxelsInChunk(chunkEntity);
    EXPECT_EQ(voxelsInChunk.size(), 11) << "8 original + 3 auto-parented from batch";

    for (auto entity : entities) {
        bool found = std::find(voxelsInChunk.begin(), voxelsInChunk.end(), entity) != voxelsInChunk.end();
        EXPECT_TRUE(found) << "Batch voxel should be auto-parented to existing chunk";
    }
}
