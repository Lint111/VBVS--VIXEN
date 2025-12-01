cmake_minimum_required(VERSION 3.20)
project(VoxelDataTests)

# Include Google Test helpers
include(GoogleTest)

# ===========================================================================
# Block Compressor Tests
# ===========================================================================
# Tests DXT1 color and normal compression/decompression

add_executable(test_block_compressor
    test_block_compressor.cpp
)

target_link_libraries(test_block_compressor
    PRIVATE
        GTest::gtest_main
        VoxelData
)

# Visual Studio solution folder organization
set_target_properties(test_block_compressor PROPERTIES
    FOLDER "Tests/VoxelData Tests"
)

gtest_discover_tests(test_block_compressor)

# ===========================================================================
# Attribute Storage Tests
# ===========================================================================
# Tests brick allocation, slot management, type safety

add_executable(test_attribute_storage
    test_attribute_storage.cpp
)

target_link_libraries(test_attribute_storage
    PRIVATE
        GTest::gtest_main
        VoxelData
)

set_target_properties(test_attribute_storage PROPERTIES
    FOLDER "Tests/VoxelData Tests"
)

gtest_discover_tests(test_attribute_storage)

# ===========================================================================
# Attribute Registry Tests
# ===========================================================================
# Tests attribute registration, key management, observers

add_executable(test_attribute_registry
    test_attribute_registry.cpp
)

target_link_libraries(test_attribute_registry
    PRIVATE
        GTest::gtest_main
        VoxelData
)

set_target_properties(test_attribute_registry PROPERTIES
    FOLDER "Tests/VoxelData Tests"
)

gtest_discover_tests(test_attribute_registry)
