if(NOT VIXEN_FAIL_SCENARIOS)
    message(STATUS "⊗ fail-scenario tests skipped (VIXEN_FAIL_SCENARIOS=OFF)")
    return()
endif()
if(TARGET GTest::gtest_main)
    add_executable(test_fail_scenario_registry FailScenarios/test_fail_scenario_registry.cpp)
    target_include_directories(test_fail_scenario_registry PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
    target_link_libraries(test_fail_scenario_registry PRIVATE GTest::gtest_main RenderGraph)
    set_target_properties(test_fail_scenario_registry PROPERTIES FOLDER "Tests/RenderGraph Tests")
    gtest_discover_tests(test_fail_scenario_registry)
    message(STATUS "✓ test_fail_scenario_registry configured")
endif()

# test_fail_scenario_sweep links VixenApp (application/main), which does not exist yet at this point
# in the configure (root CMakeLists.txt: add_subdirectory(libraries) precedes
# add_subdirectory(application)). It is configured separately from
# FailScenarios/test_fail_scenario_sweep.cmake, included from application/main/CMakeLists.txt where
# VixenApp is already a real target — see that file's tail.
