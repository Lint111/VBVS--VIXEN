cmake_minimum_required(VERSION 3.15)
project(GaiaVoxelWorldTests)

# Include Google Test helpers (provided by FetchContent in the top-level CMake)
include(GoogleTest)

# ===========================================================================
# GaiaVoxelWorld Core Tests
# ===========================================================================
# Tests entity creation, component access, spatial queries, batch operations,
# and statistics tracking.

add_executable(test_gaia_voxel_world
    test_gaia_voxel_world.cpp
)

if(TARGET GaiaVoxelWorld AND TARGET VoxelData)
    target_link_libraries(test_gaia_voxel_world PRIVATE GTest::gtest_main GaiaVoxelWorld VoxelData)
elseif(TARGET GaiaVoxelWorld)
    target_link_libraries(test_gaia_voxel_world PRIVATE GTest::gtest_main GaiaVoxelWorld)
else()
    target_link_libraries(test_gaia_voxel_world PRIVATE GTest::gtest_main)
endif()

set_target_properties(test_gaia_voxel_world PROPERTIES FOLDER "Tests/GaiaVoxelWorld Tests")

gtest_discover_tests(test_gaia_voxel_world)

# ===========================================================================
# VoxelInjectionQueue Tests
# ===========================================================================
# Tests async voxel entity creation pipeline:
# - Lock-free ring buffer enqueueing
# - Worker thread pool processing
# - Batch entity creation
# - Entity access and statistics
# - Thread safety and concurrency

add_executable(test_voxel_injection_queue
    test_voxel_injection_queue.cpp
)

if(TARGET GaiaVoxelWorld)
    target_link_libraries(test_voxel_injection_queue PRIVATE GTest::gtest_main GaiaVoxelWorld)
else()
    target_link_libraries(test_voxel_injection_queue PRIVATE GTest::gtest_main)
endif()

set_target_properties(test_voxel_injection_queue PROPERTIES FOLDER "Tests/GaiaVoxelWorld Tests")

gtest_discover_tests(test_voxel_injection_queue)

# ===========================================================================
# VoxelInjector Tests
# ===========================================================================
# Tests SVO spatial indexing of GaiaVoxelWorld entities:
# - Brick-level spatial grouping
# - Batched entity insertion
# - SVO compaction coordination
# - Integration with mock SVO

add_executable(test_voxel_injector
    test_voxel_injector.cpp
)

if(TARGET GaiaVoxelWorld)
    target_link_libraries(test_voxel_injector PRIVATE GTest::gtest_main GaiaVoxelWorld)
else()
    target_link_libraries(test_voxel_injector PRIVATE GTest::gtest_main)
endif()

set_target_properties(test_voxel_injector PROPERTIES FOLDER "Tests/GaiaVoxelWorld Tests")

gtest_discover_tests(test_voxel_injector)

# ===========================================================================
# EntityBrickView Tests
# ===========================================================================
# Tests zero-copy entity span access:
# - Linear and 3D coordinate entity access
# - Component access (density, color, normal, material)
# - Span iteration (std::span over 512 entities)
# - Utility functions (countSolidVoxels, isEmpty, isFull)
# - Index conversion (Morton code ordering)
# - Memory efficiency validation (4 KB vs 70 KB)

add_executable(test_entity_brick_view
    test_entity_brick_view.cpp
)

if(TARGET GaiaVoxelWorld)
    target_link_libraries(test_entity_brick_view PRIVATE GTest::gtest_main GaiaVoxelWorld)
else()
    target_link_libraries(test_entity_brick_view PRIVATE GTest::gtest_main)
endif()

set_target_properties(test_entity_brick_view PROPERTIES FOLDER "Tests/GaiaVoxelWorld Tests")

gtest_discover_tests(test_entity_brick_view)
