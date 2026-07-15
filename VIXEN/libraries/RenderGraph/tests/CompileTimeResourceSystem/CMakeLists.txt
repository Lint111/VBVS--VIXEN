cmake_minimum_required(VERSION 3.15)
project(CompileTimeResourceSystemTests)

# Include Google Test helpers
include(GoogleTest)

# ===========================================================================
# test_compile_time_resource_system - Merged CompileTimeResourceSystem debug
# programs (PDB consolidation: test_minimal, test_compile_time_cache,
# test_traits_debug, test_registration, test_simple_traits, test_syntax_check,
# test_type_check). Each contributes a uniquely-named run_test_X() function;
# test_compile_time_resource_system_main.cpp's int main() calls all 7 in
# sequence. Union of every constituent's include dirs/link libs (gli was
# needed by test_minimal/test_compile_time_cache/test_type_check).
# ===========================================================================
add_executable(test_compile_time_resource_system
    ../test_minimal.cpp
    test_compile_time_cache.cpp
    test_traits_debug.cpp
    test_registration.cpp
    test_simple_traits.cpp
    test_syntax_check.cpp
    test_type_check.cpp
    test_compile_time_resource_system_main.cpp
)
# Use VIXEN_ROOT for project root - link against RenderGraph for includes
target_include_directories(test_compile_time_resource_system PRIVATE ${VIXEN_ROOT}/application/main/include ${Vulkan_INCLUDE_DIRS})
target_link_libraries(test_compile_time_resource_system PRIVATE RenderGraph ${Vulkan_LIBRARIES} gli)
set_property(TARGET test_compile_time_resource_system PROPERTY CXX_STANDARD 23)
set_property(TARGET test_compile_time_resource_system PROPERTY CXX_STANDARD_REQUIRED ON)

# ===========================================================================
# Additional type system tests
# ===========================================================================
add_executable(test_recursive_validation
    test_recursive_validation.cpp
)
# Use VIXEN_ROOT for project root - no relative paths
target_include_directories(test_recursive_validation PRIVATE ${VIXEN_ROOT}/application/main/include ${Vulkan_INCLUDE_DIRS})
# Core::Core supplies Headers.h (relocated there in AR#3/#4). The sibling tests get it via
# their RenderGraph link; this one links neither, so name Core directly.
target_link_libraries(test_recursive_validation PRIVATE Core::Core ${Vulkan_LIBRARIES} gli)
set_property(TARGET test_recursive_validation PROPERTY CXX_STANDARD 23)
set_property(TARGET test_recursive_validation PROPERTY CXX_STANDARD_REQUIRED ON)

# test_cache_performance disabled - uses obsolete ResourceTypeCache.h (pre-refactor)
# add_executable(test_cache_performance
#     test_cache_performance.cpp
# )
# target_include_directories(test_cache_performance PRIVATE ${VIXEN_ROOT}/application/main/include ${Vulkan_INCLUDE_DIRS})
# target_link_libraries(test_cache_performance PRIVATE ${Vulkan_LIBRARIES})
# set_property(TARGET test_cache_performance PROPERTY CXX_STANDARD 23)
# set_property(TARGET test_cache_performance PROPERTY CXX_STANDARD_REQUIRED ON)

# Visual Studio solution folder organization
set_target_properties(
    test_compile_time_resource_system
    test_recursive_validation
    PROPERTIES FOLDER "Tests/RenderGraph Tests/CompileTimeResourceSystem"
)

# Add /FS flag for MSVC to allow parallel PDB writes
if(MSVC)
    target_compile_options(test_compile_time_resource_system PRIVATE /FS)
    target_compile_options(test_recursive_validation PRIVATE /FS)
endif()

# Register tests with CTest (these are standalone executables, not GTest-based)
# Use slash separators for hierarchical organization in Test Explorer
add_test(NAME RenderGraph/CompileTimeResourceSystem/test_compile_time_resource_system COMMAND test_compile_time_resource_system)
add_test(NAME RenderGraph/CompileTimeResourceSystem/test_recursive_validation COMMAND test_recursive_validation)

message(STATUS "[CompileTimeResourceSystem Tests] Added: 2 test executables (1 disabled, merged 7->1 for PDB consolidation)")
