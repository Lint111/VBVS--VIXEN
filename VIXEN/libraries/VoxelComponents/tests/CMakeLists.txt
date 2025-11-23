cmake_minimum_required(VERSION 3.15)
project(VoxelComponentsTests)

# Include Google Test helpers
include(GoogleTest)

# ===========================================================================
# Component System Tests
# ===========================================================================
# Tests macro-based component registry, ComponentVariant, MortonKey encoding

add_executable(test_component_system
    test_component_system.cpp
)

target_link_libraries(test_component_system
    PRIVATE
        GTest::gtest_main
        VoxelComponents
        glm::glm
)

# Visual Studio solution folder organization
set_target_properties(test_component_system PROPERTIES FOLDER "Tests/VoxelComponents Tests")

gtest_discover_tests(test_component_system)
