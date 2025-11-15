cmake_minimum_required(VERSION 3.15)
project(CompileTimeResourceSystemTests)

# Include Google Test helpers
include(GoogleTest)

# ===========================================================================
# test_minimal - Basic CompileTimeResourceSystem functionality
# ===========================================================================
add_executable(test_minimal
    ../test_minimal.cpp
)
target_include_directories(test_minimal PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../.. ${Vulkan_INCLUDE_DIRS})
target_link_libraries(test_minimal PRIVATE ${Vulkan_LIBRARIES})
set_property(TARGET test_minimal PROPERTY CXX_STANDARD 23)
set_property(TARGET test_minimal PROPERTY CXX_STANDARD_REQUIRED ON)

# ===========================================================================
# test_compile_time_cache - Compile-time type caching validation
# ===========================================================================
add_executable(test_compile_time_cache
    test_compile_time_cache.cpp
)
target_include_directories(test_compile_time_cache PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../.. ${Vulkan_INCLUDE_DIRS})
target_link_libraries(test_compile_time_cache PRIVATE ${Vulkan_LIBRARIES})
set_property(TARGET test_compile_time_cache PROPERTY CXX_STANDARD 23)
set_property(TARGET test_compile_time_cache PROPERTY CXX_STANDARD_REQUIRED ON)

# ===========================================================================
# test_traits_debug - ResourceTypeTraits debugging and validation
# ===========================================================================
add_executable(test_traits_debug
    test_traits_debug.cpp
)
target_include_directories(test_traits_debug PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../.. ${Vulkan_INCLUDE_DIRS})
target_link_libraries(test_traits_debug PRIVATE RenderGraph ${Vulkan_LIBRARIES})
set_property(TARGET test_traits_debug PROPERTY CXX_STANDARD 23)
set_property(TARGET test_traits_debug PROPERTY CXX_STANDARD_REQUIRED ON)

# ===========================================================================
# Additional type system tests
# ===========================================================================
add_executable(test_recursive_validation
    test_recursive_validation.cpp
)
target_include_directories(test_recursive_validation PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../.. ${Vulkan_INCLUDE_DIRS})
target_link_libraries(test_recursive_validation PRIVATE ${Vulkan_LIBRARIES})
set_property(TARGET test_recursive_validation PROPERTY CXX_STANDARD 23)
set_property(TARGET test_recursive_validation PROPERTY CXX_STANDARD_REQUIRED ON)

# test_cache_performance disabled - uses obsolete ResourceTypeCache.h (pre-refactor)
# add_executable(test_cache_performance
#     test_cache_performance.cpp
# )
# target_include_directories(test_cache_performance PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../.. ${Vulkan_INCLUDE_DIRS})
# target_link_libraries(test_cache_performance PRIVATE ${Vulkan_LIBRARIES})
# set_property(TARGET test_cache_performance PROPERTY CXX_STANDARD 23)
# set_property(TARGET test_cache_performance PROPERTY CXX_STANDARD_REQUIRED ON)

add_executable(test_registration
    test_registration.cpp
)
target_include_directories(test_registration PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../.. ${Vulkan_INCLUDE_DIRS})
target_link_libraries(test_registration PRIVATE RenderGraph ${Vulkan_LIBRARIES})
set_property(TARGET test_registration PROPERTY CXX_STANDARD 23)
set_property(TARGET test_registration PROPERTY CXX_STANDARD_REQUIRED ON)

add_executable(test_simple_traits
    test_simple_traits.cpp
)
target_include_directories(test_simple_traits PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../.. ${Vulkan_INCLUDE_DIRS})
target_link_libraries(test_simple_traits PRIVATE ${Vulkan_LIBRARIES})
set_property(TARGET test_simple_traits PROPERTY CXX_STANDARD 23)
set_property(TARGET test_simple_traits PROPERTY CXX_STANDARD_REQUIRED ON)

add_executable(test_syntax_check
    test_syntax_check.cpp
)
target_include_directories(test_syntax_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../.. ${Vulkan_INCLUDE_DIRS})
target_link_libraries(test_syntax_check PRIVATE RenderGraph ${Vulkan_LIBRARIES})
set_property(TARGET test_syntax_check PROPERTY CXX_STANDARD 23)
set_property(TARGET test_syntax_check PROPERTY CXX_STANDARD_REQUIRED ON)

add_executable(test_type_check
    test_type_check.cpp
)
target_include_directories(test_type_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../.. ${Vulkan_INCLUDE_DIRS})
target_link_libraries(test_type_check PRIVATE ${Vulkan_LIBRARIES})
set_property(TARGET test_type_check PROPERTY CXX_STANDARD 23)
set_property(TARGET test_type_check PROPERTY CXX_STANDARD_REQUIRED ON)

# Add /FS flag for MSVC to allow parallel PDB writes
if(MSVC)
    target_compile_options(test_minimal PRIVATE /FS)
    target_compile_options(test_compile_time_cache PRIVATE /FS)
    target_compile_options(test_traits_debug PRIVATE /FS)
    target_compile_options(test_recursive_validation PRIVATE /FS)
    target_compile_options(test_registration PRIVATE /FS)
    target_compile_options(test_simple_traits PRIVATE /FS)
    target_compile_options(test_syntax_check PRIVATE /FS)
    target_compile_options(test_type_check PRIVATE /FS)
endif()

message(STATUS "[CompileTimeResourceSystem Tests] Added: 8 test executables (1 disabled)")
