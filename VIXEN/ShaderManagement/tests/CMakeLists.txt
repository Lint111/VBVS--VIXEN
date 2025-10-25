# ShaderManagement Tests

cmake_minimum_required(VERSION 3.15)

# Find GTest
find_package(GTest REQUIRED)

enable_testing()

# Test executable
add_executable(ShaderManagementTests
    test_shader_compiler.cpp
    test_shader_bundle_builder.cpp
    test_spirv_reflection.cpp
    test_sdi_generator.cpp
    test_file_tracking.cpp
    test_logging.cpp
    test_uuid_generation.cpp
)

target_link_libraries(ShaderManagementTests
    PRIVATE
        ShaderManagement
        GTest::GTest
        GTest::Main
)

target_include_directories(ShaderManagementTests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

# Discover tests
include(GoogleTest)
gtest_discover_tests(ShaderManagementTests)

# Copy test shader files to build directory
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/test_shaders
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

message(STATUS "ShaderManagement tests configured")
