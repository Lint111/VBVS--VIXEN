# Configures test_fail_scenario_sweep. Included from application/main/CMakeLists.txt (NOT from
# libraries/RenderGraph/tests/test_fail_scenarios.cmake) because this target links VixenApp, which
# is only defined once add_subdirectory(application) runs — after add_subdirectory(libraries) has
# already processed the RenderGraph tests directory. See test_fail_scenarios.cmake for the sibling
# (library-only) test_fail_scenario_registry target.
if(NOT VIXEN_FAIL_SCENARIOS)
    return()
endif()
if(NOT BUILD_TESTS)
    return()
endif()

set(_vixen_rg_tests_dir ${VIXEN_ROOT}/libraries/RenderGraph/tests)

if(TARGET VixenApp AND TARGET GTest::gtest)
    add_executable(test_fail_scenario_sweep
        ${_vixen_rg_tests_dir}/FailScenarios/test_fail_scenario_sweep.cpp
        ${_vixen_rg_tests_dir}/FailScenarios/ScenarioHarness.cpp)
    target_include_directories(test_fail_scenario_sweep PRIVATE
        ${_vixen_rg_tests_dir}/../include
        ${_vixen_rg_tests_dir}/Nodes          # TestVkValidation.h
        ${_vixen_rg_tests_dir}/FailScenarios)
    target_link_libraries(test_fail_scenario_sweep PRIVATE GTest::gtest VixenApp glfw)
    set_target_properties(test_fail_scenario_sweep PROPERTIES FOLDER "Tests/RenderGraph Tests")
    gtest_discover_tests(test_fail_scenario_sweep PROPERTIES TIMEOUT 300)
    message(STATUS "✓ test_fail_scenario_sweep configured")
endif()

unset(_vixen_rg_tests_dir)
