# ShaderManagement Tests

cmake_minimum_required(VERSION 3.15)

# Fetch GoogleTest
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

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
        gtest
        gtest_main
)

target_include_directories(ShaderManagementTests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

# Discover tests
include(GoogleTest)
gtest_discover_tests(ShaderManagementTests)

message(STATUS "ShaderManagement tests configured")
