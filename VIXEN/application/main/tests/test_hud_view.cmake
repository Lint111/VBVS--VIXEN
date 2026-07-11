# Offline unit test for the native HudView consumer (View Contract Inc-2, Task 4). Links VixenApp
# (for RmlUi + the RenderGraph Ui/IView.h seam) which only exists after application/main/CMakeLists.txt
# has defined it — mirrors why test_app_run_tick is configured from here, not the library.
if(NOT BUILD_TESTS)
    return()
endif()

if(TARGET VixenApp AND TARGET GTest::gtest_main)
    add_executable(test_hud_view ${CMAKE_CURRENT_LIST_DIR}/test_hud_view.cpp)
    # Inc-1 M4: must whole-archive VixenApp, not link it plainly -- see cmake/VixenNodeLinkage.cmake.
    vixen_whole_archive_link_vixen_app(test_hud_view PRIVATE)
    target_link_libraries(test_hud_view PRIVATE GTest::gtest_main)
    set_target_properties(test_hud_view PROPERTIES FOLDER "Tests/Application")
    gtest_discover_tests(test_hud_view)
    message(STATUS "[Application Tests] Added: test_hud_view (HudView Register + SetHudView projection, offline)")
endif()
