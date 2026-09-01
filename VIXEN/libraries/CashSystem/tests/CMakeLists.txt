cmake_minimum_required(VERSION 3.15)
project(CashSystemTests)

# Registration API tests (new type-based registration)
add_executable(test_registration_api
    test_registration_api.cpp
)
target_compile_features(test_registration_api PRIVATE cxx_std_23)
if(TARGET CashSystem)
    target_link_libraries(test_registration_api PRIVATE GTest::gtest_main CashSystem Logger VulkanResources)
else()
    target_link_libraries(test_registration_api PRIVATE GTest::gtest_main Logger)
endif()

# Visual Studio solution folder organization
set_target_properties(test_registration_api PROPERTIES FOLDER "Tests/CashSystem Tests")

gtest_discover_tests(test_registration_api)

# Pipeline blend-state cache-key tests (AR#32)
add_executable(test_pipeline_blend_key
    test_pipeline_blend_key.cpp
)
target_compile_features(test_pipeline_blend_key PRIVATE cxx_std_23)
if(TARGET CashSystem)
    target_link_libraries(test_pipeline_blend_key PRIVATE GTest::gtest_main CashSystem Logger VulkanResources)
else()
    target_link_libraries(test_pipeline_blend_key PRIVATE GTest::gtest_main Logger)
endif()
set_target_properties(test_pipeline_blend_key PROPERTIES FOLDER "Tests/CashSystem Tests")
gtest_discover_tests(test_pipeline_blend_key)

# Phase 3 TLAS Lifecycle tests (Sprint 5)
add_executable(test_tlas_phase3
    test_tlas_phase3.cpp
)
target_compile_features(test_tlas_phase3 PRIVATE cxx_std_23)
if(TARGET CashSystem)
    target_link_libraries(test_tlas_phase3 PRIVATE GTest::gtest_main CashSystem Logger VulkanResources glm::glm)
else()
    target_link_libraries(test_tlas_phase3 PRIVATE GTest::gtest_main Logger glm::glm)
endif()

set_target_properties(test_tlas_phase3 PROPERTIES FOLDER "Tests/CashSystem Tests")
gtest_discover_tests(test_tlas_phase3)

# Phase 5.1 Lifetime/Safety tests (Sprint 5)
add_executable(test_lifetime_safety
    test_lifetime_safety.cpp
)
target_compile_features(test_lifetime_safety PRIVATE cxx_std_23)
if(TARGET CashSystem)
    target_link_libraries(test_lifetime_safety PRIVATE GTest::gtest_main CashSystem Logger VulkanResources)
else()
    target_link_libraries(test_lifetime_safety PRIVATE GTest::gtest_main Logger)
endif()

set_target_properties(test_lifetime_safety PROPERTIES FOLDER "Tests/CashSystem Tests")
gtest_discover_tests(test_lifetime_safety)

# Phase 5.3 Cacher Chain Integration tests (Sprint 5)
add_executable(test_cacher_chain_integration
    test_cacher_chain_integration.cpp
)
target_compile_features(test_cacher_chain_integration PRIVATE cxx_std_23)
if(TARGET CashSystem)
    target_link_libraries(test_cacher_chain_integration PRIVATE GTest::gtest_main CashSystem Logger VulkanResources glm::glm)
else()
    target_link_libraries(test_cacher_chain_integration PRIVATE GTest::gtest_main Logger glm::glm)
endif()

set_target_properties(test_cacher_chain_integration PROPERTIES FOLDER "Tests/CashSystem Tests")
gtest_discover_tests(test_cacher_chain_integration)

# TypedCacher concurrency regression tests (P0 / AR#51 deadlock)
add_executable(test_typed_cacher_concurrency
    test_typed_cacher_concurrency.cpp
)
target_compile_features(test_typed_cacher_concurrency PRIVATE cxx_std_23)
if(TARGET CashSystem)
    target_link_libraries(test_typed_cacher_concurrency PRIVATE GTest::gtest_main CashSystem Logger VulkanResources)
else()
    target_link_libraries(test_typed_cacher_concurrency PRIVATE GTest::gtest_main Logger)
endif()

set_target_properties(test_typed_cacher_concurrency PROPERTIES FOLDER "Tests/CashSystem Tests")
gtest_discover_tests(test_typed_cacher_concurrency)

# VoxelSceneCreateInfo cache-key tests (P0 / AR#52 custom-generator identity)
add_executable(test_voxel_scene_key
    test_voxel_scene_key.cpp
)
target_compile_features(test_voxel_scene_key PRIVATE cxx_std_23)
if(TARGET CashSystem)
    target_link_libraries(test_voxel_scene_key PRIVATE GTest::gtest_main CashSystem Logger VulkanResources glm::glm)
else()
    target_link_libraries(test_voxel_scene_key PRIVATE GTest::gtest_main Logger glm::glm)
endif()

set_target_properties(test_voxel_scene_key PROPERTIES FOLDER "Tests/CashSystem Tests")
gtest_discover_tests(test_voxel_scene_key)

# Shared bounds-checked cache codec tests (audit V-M5..M8, N6, N7)
add_executable(test_cache_codec
    test_cache_codec.cpp
)
target_compile_features(test_cache_codec PRIVATE cxx_std_23)
if(TARGET CashSystem)
    target_link_libraries(test_cache_codec PRIVATE GTest::gtest_main CashSystem Logger)
else()
    target_link_libraries(test_cache_codec PRIVATE GTest::gtest_main Logger)
endif()
set_target_properties(test_cache_codec PROPERTIES FOLDER "Tests/CashSystem Tests")
gtest_discover_tests(test_cache_codec)

# SPIR-V loader hardening tests (audit V-M7)
add_executable(test_spirv_loader
    test_spirv_loader.cpp
)
target_compile_features(test_spirv_loader PRIVATE cxx_std_23)
if(TARGET CashSystem)
    target_link_libraries(test_spirv_loader PRIVATE GTest::gtest_main CashSystem Logger VulkanResources)
else()
    target_link_libraries(test_spirv_loader PRIVATE GTest::gtest_main Logger)
endif()
set_target_properties(test_spirv_loader PROPERTIES FOLDER "Tests/CashSystem Tests")
gtest_discover_tests(test_spirv_loader)

# Pipeline-cache blob header/UUID validation tests (audit V-M8)
add_executable(test_pipeline_cache_header
    test_pipeline_cache_header.cpp
)
target_compile_features(test_pipeline_cache_header PRIVATE cxx_std_23)
if(TARGET CashSystem)
    target_link_libraries(test_pipeline_cache_header PRIVATE GTest::gtest_main CashSystem Logger VulkanResources)
else()
    target_link_libraries(test_pipeline_cache_header PRIVATE GTest::gtest_main Logger)
endif()
set_target_properties(test_pipeline_cache_header PROPERTIES FOLDER "Tests/CashSystem Tests")
gtest_discover_tests(test_pipeline_cache_header)

# Basic CashSystem functionality tests
# TODO: Update these tests to use new registration-by-type API
# add_executable(test_cashsystem_basic
#     test_cashsystem_basic.cpp
#     test_basic_functionality.cpp
# )
# target_compile_features(test_cashsystem_basic PRIVATE cxx_std_23)
# if(TARGET CashSystem)
#     target_link_libraries(test_cashsystem_basic PRIVATE GTest::gtest_main CashSystem Logger)
# else()
#     target_link_libraries(test_cashsystem_basic PRIVATE GTest::gtest_main Logger)
# endif()
#
# gtest_discover_tests(test_cashsystem_basic)

# Type registry and factory pattern tests
# TODO: Update these tests to use new registration-by-type API
# add_executable(test_cashsystem_registry
#     test_cashsystem_registry.cpp
#     test_type_registry.cpp
#     test_factory_pattern.cpp
# )
# target_compile_features(test_cashsystem_registry PRIVATE cxx_std_23)
# if(TARGET CashSystem)
#     target_link_libraries(test_cashsystem_registry PRIVATE GTest::gtest_main CashSystem Logger)
# else()
#     target_link_libraries(test_cashsystem_registry PRIVATE GTest::gtest_main Logger)
# endif()
#
# gtest_discover_tests(test_cashsystem_registry)

# Multi-device and hybrid caching tests
# TODO: Update these tests to use new registration-by-type API
# add_executable(test_cashsystem_multidevice
#     test_cashsystem_multidevice.cpp
#     test_device_registries.cpp
#     test_hybrid_caching.cpp
# )
# target_compile_features(test_cashsystem_multidevice PRIVATE cxx_std_23)
# if(TARGET CashSystem)
#     target_link_libraries(test_cashsystem_multidevice PRIVATE GTest::gtest_main CashSystem Logger)
# else()
#     target_link_libraries(test_cashsystem_multidevice PRIVATE GTest::gtest_main Logger)
# endif()
#
# gtest_discover_tests(test_cashsystem_multidevice)

# Caching performance and integration tests
# TODO: Update these tests to use new registration-by-type API
# add_executable(test_cashsystem_integration
#     test_cashsystem_integration.cpp
#     test_performance.cpp
#     test_integration_rendergraph.cpp
# )
# target_compile_features(test_cashsystem_integration PRIVATE cxx_std_23)
# if(TARGET CashSystem)
#     target_link_libraries(test_cashsystem_integration PRIVATE GTest::gtest_main CashSystem Logger RenderGraph)
# else()
#     target_link_libraries(test_cashsystem_integration PRIVATE GTest::gtest_main Logger)
# endif()
#
# gtest_discover_tests(test_cashsystem_integration)

# Main test orchestrator
# TODO: Update these tests to use new registration-by-type API
# add_executable(test_cashsystem_all
#     test_cashsystem_all.cpp
# )
# target_compile_features(test_cashsystem_all PRIVATE cxx_std_23)
# if(TARGET CashSystem)
#     target_link_libraries(test_cashsystem_all PRIVATE GTest::gtest_main CashSystem Logger)
# else()
#     target_link_libraries(test_cashsystem_all PRIVATE GTest::gtest_main Logger)
# endif()
#
# gtest_discover_tests(test_cashsystem_all)

# Recipe-bytecode content-hash tests (Recipe Pipeline Cache Inc1, M1)
add_executable(test_recipe_content_hash
    test_recipe_content_hash.cpp
)
target_compile_features(test_recipe_content_hash PRIVATE cxx_std_23)
if(TARGET CashSystem)
    target_link_libraries(test_recipe_content_hash PRIVATE GTest::gtest_main CashSystem Logger VulkanResources)
else()
    target_link_libraries(test_recipe_content_hash PRIVATE GTest::gtest_main Logger)
endif()
set_target_properties(test_recipe_content_hash PROPERTIES FOLDER "Tests/CashSystem Tests")
gtest_discover_tests(test_recipe_content_hash)

# Recipe-content cacher tests (Recipe Pipeline Cache Inc1, M2)
add_executable(test_recipe_content_cacher
    test_recipe_content_cacher.cpp
)
target_compile_features(test_recipe_content_cacher PRIVATE cxx_std_23)
if(TARGET CashSystem)
    target_link_libraries(test_recipe_content_cacher PRIVATE GTest::gtest_main CashSystem Logger VulkanResources)
else()
    target_link_libraries(test_recipe_content_cacher PRIVATE GTest::gtest_main Logger)
endif()
set_target_properties(test_recipe_content_cacher PROPERTIES FOLDER "Tests/CashSystem Tests")
gtest_discover_tests(test_recipe_content_cacher)

# Generation-keyed recipe bucket table cacher and dirty-range publication tests (Row A / 0eb.3)
add_executable(test_recipe_bucket_data_cacher
    test_recipe_bucket_data_cacher.cpp
)
target_compile_features(test_recipe_bucket_data_cacher PRIVATE cxx_std_23)
if(TARGET CashSystem)
    target_link_libraries(test_recipe_bucket_data_cacher PRIVATE GTest::gtest_main CashSystem Logger VulkanResources)
else()
    target_link_libraries(test_recipe_bucket_data_cacher PRIVATE GTest::gtest_main Logger)
endif()
set_target_properties(test_recipe_bucket_data_cacher PROPERTIES FOLDER "Tests/CashSystem Tests")
gtest_discover_tests(test_recipe_bucket_data_cacher)
