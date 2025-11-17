# ===========================================================================
# Voxel Octree Tests
# ===========================================================================
# This test suite validates VoxelOctree (Phase H):
# - Octree construction from voxel grids
# - Empty space culling optimization
# - Compression ratio calculation
# - Serialization/deserialization
# - Edge cases (power-of-2 validation, empty grids, filled grids)
# Compatible with VULKAN_TRIMMED_BUILD (headers only).

add_executable(test_voxel_octree
    Data/test_voxel_octree.cpp
)

# Allow tests to include library headers with clean paths: #include "RenderGraph/..."
target_include_directories(test_voxel_octree PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include  # RenderGraph's own headers
)

target_link_libraries(test_voxel_octree PRIVATE
    GTest::gtest_main
    RenderGraph
)

gtest_discover_tests(test_voxel_octree)

message(STATUS "[RenderGraph Tests] Added: test_voxel_octree")
