# Offline unit test for VulkanApplicationBase::Tick()/Run() classification. Links VixenApp (the app
# target) which only exists after application/main/CMakeLists.txt has defined it — mirrors why
# test_fail_scenario_sweep is configured from application/main/CMakeLists.txt, not the library.
# No GPU: the stub subclass overrides Render()/Update() with canned outcomes.
if(NOT BUILD_TESTS)
    return()
endif()

if(TARGET VixenApp AND TARGET GTest::gtest_main)
    add_executable(test_app_run_tick ${CMAKE_CURRENT_LIST_DIR}/test_app_run_tick.cpp)
    # Inc-1 M4: must whole-archive VixenApp, not link it plainly -- see cmake/VixenNodeLinkage.cmake.
    vixen_whole_archive_link_vixen_app(test_app_run_tick PRIVATE)
    target_link_libraries(test_app_run_tick PRIVATE GTest::gtest_main)
    set_target_properties(test_app_run_tick PROPERTIES FOLDER "Tests/Application")
    gtest_discover_tests(test_app_run_tick)
    message(STATUS "[Application Tests] Added: test_app_run_tick (Tick/Run classification, offline)")
endif()
