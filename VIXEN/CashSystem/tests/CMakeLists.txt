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

gtest_discover_tests(test_registration_api)

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