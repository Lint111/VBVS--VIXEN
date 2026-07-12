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
    test_shader_cache_manager.cpp
    test_sdi_lifecycle.cpp
    test_shader_preprocessor.cpp
    # Temporarily disabled - need API updates
    # test_shader_bundle_builder.cpp
    # test_spirv_reflection.cpp
    # test_sdi_generator.cpp
    # test_file_tracking.cpp
    # test_logging.cpp
    # test_uuid_generation.cpp
)

# HLSL ingestion test (P0 T5) — separate executable so it has no Vulkan device dependency
add_executable(test_hlsl_ingestion
    test_hlsl_ingestion.cpp
)
target_link_libraries(test_hlsl_ingestion
    PRIVATE
        ShaderManagement
        gtest
        gtest_main
)
set_target_properties(test_hlsl_ingestion PROPERTIES FOLDER "Tests/ShaderManagement Tests")
# Pass the absolute path to the vendored generated HLSL at compile time
target_compile_definitions(test_hlsl_ingestion PRIVATE
    SDF_CORE_KERNELS_HLSL_PATH="${CMAKE_SOURCE_DIR}/libraries/SVO/shaders/recipe/SdfCoreKernels.g.hlsl"
)
include(GoogleTest)
gtest_discover_tests(test_hlsl_ingestion WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

# Probe-update reusability smoke test (Sampled Lighting Inc4 M1) — separate
# executable, no Vulkan device dependency, mirrors test_hlsl_ingestion above.
add_executable(test_probe_update_smoke
    test_probe_update_smoke.cpp
)
target_link_libraries(test_probe_update_smoke
    PRIVATE
        ShaderManagement
        gtest
        gtest_main
)
set_target_properties(test_probe_update_smoke PROPERTIES FOLDER "Tests/ShaderManagement Tests")
target_compile_definitions(test_probe_update_smoke PRIVATE
    VIXEN_SHADERS_DIR="${CMAKE_SOURCE_DIR}/shaders")
include(GoogleTest)
gtest_discover_tests(test_probe_update_smoke WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

# Link against library target - includes propagate automatically
target_link_libraries(ShaderManagementTests
    PRIVATE
        ShaderManagement
        gtest
        gtest_main
)

# Visual Studio solution folder organization
set_target_properties(ShaderManagementTests PROPERTIES FOLDER "Tests/ShaderManagement Tests")

# Discover tests
include(GoogleTest)
gtest_discover_tests(ShaderManagementTests
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)

message(STATUS "ShaderManagement tests configured")
