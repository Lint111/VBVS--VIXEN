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

gtest_discover_tests(test_svo_builder)

# ===========================================================================
# Voxel Injection Tests
# ===========================================================================
# Tests voxel-based octree construction, procedural samplers

add_executable(test_voxel_injection
    test_voxel_injection.cpp
)

if(TARGET SVO)
    target_link_libraries(test_voxel_injection PRIVATE GTest::gtest_main SVO)
else()
    target_link_libraries(test_voxel_injection PRIVATE GTest::gtest_main)
endif()

set_target_properties(test_voxel_injection PROPERTIES FOLDER "Tests/SVO Tests")

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

gtest_discover_tests(test_samplers)
