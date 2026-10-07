if(NOT BUILD_TESTS)
    return()
endif()

if(TARGET VixenApp AND TARGET GTest::gtest_main)
    add_executable(vixen_lookdev_capture
        ${CMAKE_CURRENT_LIST_DIR}/test_lookdev_capture.cpp)
    vixen_whole_archive_link_vixen_app(vixen_lookdev_capture PRIVATE)
    target_link_libraries(vixen_lookdev_capture PRIVATE GTest::gtest_main)
    set_target_properties(vixen_lookdev_capture PROPERTIES FOLDER "Tests/Application")
    set(_lookdev_test_environment
        ${VIXEN_CTEST_GPU_ENVIRONMENT}
        "VIXEN_HDR_EXPOSURE=1"
        "VIXEN_HUD_SUPPRESS=1")
    add_test(NAME Application.LookdevCapture.RendersFourStatesAtFourAnglesDeterministically
        COMMAND ${CMAKE_COMMAND}
            "-DLOOKDEV_CAPTURE_EXE=$<TARGET_FILE:vixen_lookdev_capture>"
            "-DLOOKDEV_CAPTURE_DIR=${CMAKE_BINARY_DIR}/runtime-captures/lookdev"
            -P "${CMAKE_CURRENT_LIST_DIR}/run_lookdev_capture.cmake")
    set_tests_properties(Application.LookdevCapture.RendersFourStatesAtFourAnglesDeterministically
        PROPERTIES
            LABELS "Application"
            TIMEOUT 1200
            ENVIRONMENT "${_lookdev_test_environment}"
            ENVIRONMENT_MODIFICATION
                "VIXEN_TEST_CELSHADE_LAMBERT_GGX=unset:;VIXEN_DDGI_CORNELL_BAKED_DEMO=unset:;VIXEN_DDGI_CORNELL_VIRTUAL_DEMO=unset:;VIXEN_DDGI_CORNELL_HYBRID_DEMO=unset:;VIXEN_DDGI_CORNELL_MIXED_DEMO=unset:")
    message(STATUS "[Application Tests] Added: vixen_lookdev_capture (16 states/angles, repeat-pixel witness)")
endif()
