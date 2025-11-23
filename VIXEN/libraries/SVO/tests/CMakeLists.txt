cmake_minimum_required(VERSION 3.15)
project(SVOTests)

# Include Google Test helpers (provided by FetchContent in the top-level CMake)
include(GoogleTest)

# ===========================================================================
# Basic SVO Builder Tests
# ===========================================================================
# Tests mesh-based octree construction, triangle filtering, SAT

add_executable(test_svo_builder
    test_svo_builder.cpp
)

if(TARGET SVO)
    target_link_libraries(test_svo_builder PRIVATE GTest::gtest_main SVO)
else()
    target_link_libraries(test_svo_builder PRIVATE GTest::gtest_main)
endif()

# Visual Studio solution folder organization
set_target_properties(test_svo_builder PROPERTIES FOLDER "Tests/SVO Tests")

# Copy TBB DLLs to test executable directory for runtime
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_svo_builder POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb>
            $<TARGET_FILE_DIR:test_svo_builder>
        COMMENT "Copying TBB DLL for test_svo_builder"
    )
endif()

gtest_discover_tests(test_svo_builder)

# ===========================================================================
# Voxel Injection Tests
# ===========================================================================
# Tests voxel-based octree construction, procedural samplers

add_executable(test_voxel_injection
    test_voxel_injection.cpp
)

if(TARGET SVO)
    target_link_libraries(test_voxel_injection PRIVATE GTest::gtest_main SVO GaiaVoxelWorld)
else()
    target_link_libraries(test_voxel_injection PRIVATE GTest::gtest_main)
endif()

set_target_properties(test_voxel_injection PROPERTIES FOLDER "Tests/SVO Tests")

# Copy TBB DLLs to test executable directory for runtime
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_voxel_injection POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb>
            $<TARGET_FILE_DIR:test_voxel_injection>
        COMMENT "Copying TBB DLL for test_voxel_injection"
    )
endif()

gtest_discover_tests(test_voxel_injection)

# ===========================================================================
# Data Structures Tests
# ===========================================================================
# Tests ChildDescriptor, Contour encoding/decoding, attributes

add_executable(test_svo_types
    test_svo_types.cpp
)

if(TARGET SVO)
    target_link_libraries(test_svo_types PRIVATE GTest::gtest_main SVO)
else()
    target_link_libraries(test_svo_types PRIVATE GTest::gtest_main)
endif()

set_target_properties(test_svo_types PROPERTIES FOLDER "Tests/SVO Tests")

# Copy TBB DLLs to test executable directory for runtime
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_svo_types POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb>
            $<TARGET_FILE_DIR:test_svo_types>
        COMMENT "Copying TBB DLL for test_svo_types"
    )
endif()

gtest_discover_tests(test_svo_types)

# ===========================================================================
# Sampler Tests
# ===========================================================================
# Tests NoiseSampler, SDFSampler, HeightmapSampler

add_executable(test_samplers
    test_samplers.cpp
)

if(TARGET SVO)
    target_link_libraries(test_samplers PRIVATE GTest::gtest_main SVO)
else()
    target_link_libraries(test_samplers PRIVATE GTest::gtest_main)
endif()

set_target_properties(test_samplers PROPERTIES FOLDER "Tests/SVO Tests")

# Copy TBB DLLs to test executable directory for runtime
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_samplers POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb>
            $<TARGET_FILE_DIR:test_samplers>
        COMMENT "Copying TBB DLL for test_samplers"
    )
endif()

gtest_discover_tests(test_samplers)

# ===========================================================================
# Octree Query Interface Tests
# ===========================================================================
# Tests voxelExists, getVoxelData, getChildMask, castRay, castRayLOD

add_executable(test_octree_queries
    test_octree_queries.cpp
)

if(TARGET SVO)
    target_link_libraries(test_octree_queries PRIVATE GTest::gtest_main SVO GaiaVoxelWorld VoxelComponents)
else()
    target_link_libraries(test_octree_queries PRIVATE GTest::gtest_main)
endif()

set_target_properties(test_octree_queries PROPERTIES FOLDER "Tests/SVO Tests")

# Copy TBB DLLs to test executable directory for runtime
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_octree_queries POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb>
            $<TARGET_FILE_DIR:test_octree_queries>
        COMMENT "Copying TBB DLL for test_octree_queries"
    )
endif()

gtest_discover_tests(test_octree_queries)

# ===========================================================================
# BrickView Tests (Converted from Legacy BrickStorage)
# ===========================================================================
# Tests AttributeRegistry + BrickView integration:
# - Brick allocation and indexing
# - Multi-attribute access (Float, Uint32, Vec3)
# - 3D coordinate API (setAt3D / getAt3D)
# - Pointer-based access (zero-cost path)
# - Index-based access (AttributeIndex for O(1) lookup)
# - Data isolation between bricks
# - Backward compatibility (string-based lookup)

add_executable(test_brick_view
    test_brick_view.cpp
)

if(TARGET VoxelData)
    target_link_libraries(test_brick_view PRIVATE GTest::gtest_main VoxelData)
else()
    target_link_libraries(test_brick_view PRIVATE GTest::gtest_main)
endif()

set_target_properties(test_brick_view PROPERTIES FOLDER "Tests/VoxelData Tests")

# Copy TBB DLLs to test executable directory for runtime
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_brick_view POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb>
            $<TARGET_FILE_DIR:test_brick_view>
        COMMENT "Copying TBB DLL for test_brick_view"
    )
endif()

gtest_discover_tests(test_brick_view)

# ===========================================================================
# EntityBrickView Tests (Spatial View Pattern)
# ===========================================================================
# Tests entity-based brick view pattern (SVO spatial indexing concern):
# - Zero-copy entity span access over 8³ brick regions
# - Linear and 3D coordinate entity access
# - Component access (density, color, normal, material)
# - Span iteration (std::span over 512 entities)
# - Utility functions (countSolidVoxels, isEmpty, isFull)
# - Memory efficiency validation (4 KB entity refs vs 70 KB data copies)

add_executable(test_entity_brick_view
    test_entity_brick_view.cpp
)

target_link_libraries(test_entity_brick_view PRIVATE
    GTest::gtest_main
    GaiaVoxelWorld
    VoxelData
)

set_target_properties(test_entity_brick_view PROPERTIES FOLDER "Tests/SVO Tests")

# Copy TBB DLLs to test executable directory for runtime
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_entity_brick_view POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb>
            $<TARGET_FILE_DIR:test_entity_brick_view>
        COMMENT "Copying TBB DLL for test_entity_brick_view"
    )
endif()

gtest_discover_tests(test_entity_brick_view)

# ===========================================================================
# Comprehensive Ray Casting Tests
# ===========================================================================
# Tests axis-aligned rays, diagonal rays, inside/outside grid, miss cases,
# multiple voxel traversal, dense volumes, edge cases, stress testing,
# performance characteristics, and Cornell box scene rendering

add_executable(test_ray_casting_comprehensive
    test_ray_casting_comprehensive.cpp
)

if(TARGET SVO)
    target_link_libraries(test_ray_casting_comprehensive PRIVATE GTest::gtest_main SVO)
else()
    target_link_libraries(test_ray_casting_comprehensive PRIVATE GTest::gtest_main)
endif()

set_target_properties(test_ray_casting_comprehensive PROPERTIES FOLDER "Tests/SVO Tests")

# Copy TBB DLLs to test executable directory for runtime
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_ray_casting_comprehensive POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb>
            $<TARGET_FILE_DIR:test_ray_casting_comprehensive>
        COMMENT "Copying TBB DLL for test_ray_casting_comprehensive"
    )
endif()

gtest_discover_tests(test_ray_casting_comprehensive)

# ===========================================================================
# Brick Creation Tests
# ===========================================================================
# Tests that bricks are properly allocated and populated during voxel injection,
# and that ray casting finds and traverses them

add_executable(test_brick_creation
    test_brick_creation.cpp
)

if(TARGET SVO)
    target_link_libraries(test_brick_creation PRIVATE GTest::gtest_main SVO)
else()
    target_link_libraries(test_brick_creation PRIVATE GTest::gtest_main)
endif()

set_target_properties(test_brick_creation PROPERTIES FOLDER "Tests/SVO Tests")

# Copy TBB DLLs to test executable directory for runtime
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_brick_creation POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb>
            $<TARGET_FILE_DIR:test_brick_creation>
        COMMENT "Copying TBB DLL for test_brick_creation"
    )
endif()

gtest_discover_tests(test_brick_creation)

# ===========================================================================
# AttributeRegistry Integration Tests
# ===========================================================================
# Tests LaineKarrasOctree integration with AttributeRegistry after migration
# from BrickStorage. Validates key attribute lookup, multi-attribute access,
# BrickView pointer access, type safety, and custom key predicates.

add_executable(test_attribute_registry_integration
    test_attribute_registry_integration.cpp
)

if(TARGET SVO AND TARGET VoxelData)
    target_link_libraries(test_attribute_registry_integration PRIVATE GTest::gtest_main SVO VoxelData)
elseif(TARGET SVO)
    target_link_libraries(test_attribute_registry_integration PRIVATE GTest::gtest_main SVO)
else()
    target_link_libraries(test_attribute_registry_integration PRIVATE GTest::gtest_main)
endif()

set_target_properties(test_attribute_registry_integration PROPERTIES FOLDER "Tests/SVO Tests")

# Copy TBB DLLs to test executable directory for runtime
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_attribute_registry_integration POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb>
            $<TARGET_FILE_DIR:test_attribute_registry_integration>
        COMMENT "Copying TBB DLL for test_attribute_registry_integration"
    )
endif()

gtest_discover_tests(test_attribute_registry_integration)

# ===========================================================================
# Brick Traversal Tests
# ===========================================================================
# Tests brick DDA traversal within LaineKarrasOctree. Validates brick-to-leaf
# transitions, brick misses with grid continuation, multi-brick traversal,
# brick boundary conditions, and axis-parallel rays through bricks.

add_executable(test_brick_traversal
    test_brick_traversal.cpp
)

if(TARGET SVO)
    target_link_libraries(test_brick_traversal PRIVATE GTest::gtest_main SVO)
else()
    target_link_libraries(test_brick_traversal PRIVATE GTest::gtest_main)
endif()

set_target_properties(test_brick_traversal PROPERTIES FOLDER "Tests/SVO Tests")

# Copy TBB DLLs to test executable directory for runtime
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_brick_traversal POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb>
            $<TARGET_FILE_DIR:test_brick_traversal>
        COMMENT "Copying TBB DLL for test_brick_traversal"
    )
endif()

gtest_discover_tests(test_brick_traversal)
