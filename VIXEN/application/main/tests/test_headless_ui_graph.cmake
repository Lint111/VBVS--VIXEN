if(NOT BUILD_TESTS)
    return()
endif()

if(TARGET VixenApp AND TARGET GTest::gtest_main)
    add_executable(test_headless_ui_graph ${CMAKE_CURRENT_LIST_DIR}/test_headless_ui_graph.cpp)
    vixen_whole_archive_link_vixen_app(test_headless_ui_graph PRIVATE)
    target_link_libraries(test_headless_ui_graph PRIVATE GTest::gtest_main)
    set_target_properties(test_headless_ui_graph PROPERTIES FOLDER "Tests/Application")
    vixen_gtest_discover_tests(Application test_headless_ui_graph
        TEST_ENVIRONMENT "VIXEN_HEADLESS_UI_CAPTURE=${CMAKE_BINARY_DIR}/headless-ui-frame.png")
    message(STATUS "[Application Tests] Added: test_headless_ui_graph (offscreen UI render/readback)")
endif()
