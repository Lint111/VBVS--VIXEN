# ===========================================================================
# Voxel Traversal Tests (Phase H.4.5)
# ===========================================================================
# This test suite validates voxel traversal algorithms:
# - Ray-AABB intersection (Williams et al. 2005)
# - DDA voxel traversal (Amanatides & Woo 1987)
# - Empty space skipping optimization
# Compatible with VULKAN_TRIMMED_BUILD (headers only).

add_executable(test_voxel_traversal
    Data/test_voxel_traversal.cpp
)

# Allow tests to include library headers with clean paths: #include "RenderGraph/..."
target_include_directories(test_voxel_traversal PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include  # RenderGraph's own headers
)

target_link_libraries(test_voxel_traversal PRIVATE
    GTest::gtest_main
    RenderGraph
)

gtest_discover_tests(test_voxel_traversal)

message(STATUS "[RenderGraph Tests] Added: test_voxel_traversal")
