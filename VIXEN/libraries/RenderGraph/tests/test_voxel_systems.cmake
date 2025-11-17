# ===========================================================================
# Voxel Systems - Consolidated Test Configuration (Phase H)
# ===========================================================================
# This file consolidates all voxel-related system tests:
# - Voxel Octree: Construction, compression, serialization
# - Scene Generators: Procedural scene generation with density targets
# - Voxel Traversal: Ray-AABB intersection and DDA traversal
#
# Compatible with VULKAN_TRIMMED_BUILD (headers only, no Vulkan required).
# ===========================================================================

# ---------------------------------------------------------------------------
# Voxel Octree Tests (Phase H)
# ---------------------------------------------------------------------------
# Validates VoxelOctree functionality:
# - Octree construction from voxel grids
# - Empty space culling optimization
# - Compression ratio calculation
# - Serialization/deserialization
# - Edge cases (power-of-2 validation, empty grids, filled grids)

add_executable(test_voxel_octree
    Data/test_voxel_octree.cpp
)

target_link_libraries(test_voxel_octree PRIVATE
    GTest::gtest_main
    RenderGraph
)

gtest_discover_tests(test_voxel_octree)

message(STATUS "[RenderGraph Tests] Added: test_voxel_octree")

# ---------------------------------------------------------------------------
# Scene Generator Tests (Phase H.2.5)
# ---------------------------------------------------------------------------
# Validates procedural scene generation:
# - Cornell Box (10% density ±5%)
# - Cave System (50% density ±5%)
# - Urban Grid (90% density ±5%)
# - Reproducibility and spatial coherence

add_executable(test_scene_generators
    Data/test_scene_generators.cpp
)

# Allow tests to include library headers with clean paths: #include "RenderGraph/..."
target_include_directories(test_scene_generators PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include  # RenderGraph's own headers
)

target_link_libraries(test_scene_generators PRIVATE
    GTest::gtest_main
    RenderGraph
)

gtest_discover_tests(test_scene_generators)

message(STATUS "[RenderGraph Tests] Added: test_scene_generators")

# ---------------------------------------------------------------------------
# Voxel Traversal Tests (Phase H.4.5)
# ---------------------------------------------------------------------------
# Validates voxel traversal algorithms:
# - Ray-AABB intersection (Williams et al. 2005)
# - DDA voxel traversal (Amanatides & Woo 1987)
# - Empty space skipping optimization

add_executable(test_voxel_traversal
    Data/test_voxel_traversal.cpp
)

target_link_libraries(test_voxel_traversal PRIVATE
    GTest::gtest_main
    RenderGraph
)

gtest_discover_tests(test_voxel_traversal)

message(STATUS "[RenderGraph Tests] Added: test_voxel_traversal")
