if(NOT BUILD_TESTS)
    return()
endif()

if(TARGET VixenApp AND TARGET GTest::gtest_main)
    add_executable(test_headless_starfield_graph
        ${CMAKE_CURRENT_LIST_DIR}/test_headless_starfield_graph.cpp)
    vixen_whole_archive_link_vixen_app(test_headless_starfield_graph PRIVATE)
    target_link_libraries(test_headless_starfield_graph PRIVATE GTest::gtest_main stb)
    set_target_properties(test_headless_starfield_graph PROPERTIES FOLDER "Tests/Application")

    set(_starfield_prefix "${CMAKE_BINARY_DIR}/headless-starfield-frame")
    add_test(NAME HeadlessStarfieldGraph.CaptureFirst
        COMMAND test_headless_starfield_graph
            --gtest_filter=HeadlessStarfieldGraph.CaptureSingleRun)
    set_tests_properties(HeadlessStarfieldGraph.CaptureFirst PROPERTIES
        ENVIRONMENT "VIXEN_HEADLESS_STARFIELD_CAPTURE_PREFIX=${_starfield_prefix}"
        ENVIRONMENT_MODIFICATION "VIXEN_DDGI_CORNELL_VIRTUAL_DEMO=set:1;VIXEN_TEST_CELSHADE_LAMBERT_GGX=set:1"
        LABELS "Application"
        RUN_SERIAL TRUE)

    add_test(NAME HeadlessStarfieldGraph.CaptureSecond
        COMMAND test_headless_starfield_graph
            --gtest_filter=HeadlessStarfieldGraph.CaptureSingleRun)
    set_tests_properties(HeadlessStarfieldGraph.CaptureSecond PROPERTIES
        ENVIRONMENT "VIXEN_HEADLESS_STARFIELD_CAPTURE_PREFIX=${_starfield_prefix}-repeat"
        ENVIRONMENT_MODIFICATION "VIXEN_DDGI_CORNELL_VIRTUAL_DEMO=set:1;VIXEN_TEST_CELSHADE_LAMBERT_GGX=set:1"
        LABELS "Application"
        RUN_SERIAL TRUE)

    add_test(NAME HeadlessStarfieldGraph.CompareIndependentCaptures
        COMMAND test_headless_starfield_graph
            --gtest_filter=HeadlessStarfieldGraph.IndependentRunsHaveIdenticalPixelsAndToggleChangesOnlyBackground)
    set_tests_properties(HeadlessStarfieldGraph.CompareIndependentCaptures PROPERTIES
        ENVIRONMENT "VIXEN_HEADLESS_STARFIELD_CAPTURE_PREFIX=${_starfield_prefix}"
        ENVIRONMENT_MODIFICATION "VIXEN_TEST_CELSHADE_LAMBERT_GGX=set:1"
        DEPENDS "HeadlessStarfieldGraph.CaptureFirst;HeadlessStarfieldGraph.CaptureSecond"
        LABELS "Application"
        RUN_SERIAL TRUE)

    message(STATUS "[Application Tests] Added: test_headless_starfield_graph (isolated production graph captures and pixel comparison)")
endif()
