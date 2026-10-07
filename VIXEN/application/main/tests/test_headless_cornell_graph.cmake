if(NOT BUILD_TESTS)
    return()
endif()

if(TARGET VixenApp AND TARGET GTest::gtest_main)
    add_executable(test_headless_cornell_graph
        ${CMAKE_CURRENT_LIST_DIR}/test_headless_cornell_graph.cpp)
    vixen_whole_archive_link_vixen_app(test_headless_cornell_graph PRIVATE)
    target_link_libraries(test_headless_cornell_graph PRIVATE GTest::gtest_main stb)
    set_target_properties(test_headless_cornell_graph PROPERTIES FOLDER "Tests/Application")
    vixen_gtest_discover_tests(Application test_headless_cornell_graph
        TEST_ENVIRONMENT "VIXEN_HEADLESS_CORNELL_CAPTURE_PREFIX=${CMAKE_BINARY_DIR}/headless-cornell-frame"
        TEST_ENVIRONMENT_MODIFICATION "VIXEN_DDGI_CORNELL_VIRTUAL_DEMO=set:1;VIXEN_TEST_CELSHADE_LAMBERT_GGX=set:1")
    message(STATUS "[Application Tests] Added: test_headless_cornell_graph (production graph offscreen render/readback)")
endif()
