if(NOT BUILD_TESTS)
    return()
endif()

if(TARGET VixenApp AND TARGET GTest::gtest_main)
    add_executable(vixen_planet_capture
        ${CMAKE_CURRENT_LIST_DIR}/test_planet_capture.cpp)
    vixen_whole_archive_link_vixen_app(vixen_planet_capture PRIVATE)
    target_link_libraries(vixen_planet_capture PRIVATE GTest::gtest_main)
    set_target_properties(vixen_planet_capture PROPERTIES FOLDER "Tests/Application")
    set(_planet_test_environment
        ${VIXEN_CTEST_GPU_ENVIRONMENT}
        "VIXEN_HDR_EXPOSURE=1"
        "VIXEN_HUD_SUPPRESS=1")
    add_test(NAME Application.PlanetCapture.RendersStatesDeterministically
        COMMAND ${CMAKE_COMMAND}
            "-DPLANET_CAPTURE_EXE=$<TARGET_FILE:vixen_planet_capture>"
            "-DPLANET_CAPTURE_DIR=${CMAKE_BINARY_DIR}/runtime-captures/planet"
            -P "${CMAKE_CURRENT_LIST_DIR}/run_planet_capture.cmake")
    set_tests_properties(Application.PlanetCapture.RendersStatesDeterministically
        PROPERTIES
            LABELS "Application"
            TIMEOUT 3600
            ENVIRONMENT "${_planet_test_environment}"
            ENVIRONMENT_MODIFICATION
                "VIXEN_TEST_CELSHADE_LAMBERT_GGX=unset:;VIXEN_DDGI_CORNELL_BAKED_DEMO=unset:;VIXEN_DDGI_CORNELL_VIRTUAL_DEMO=unset:;VIXEN_DDGI_CORNELL_HYBRID_DEMO=unset:;VIXEN_DDGI_CORNELL_MIXED_DEMO=unset:")
    vixen_lock_gpu_tests(TESTS Application.PlanetCapture.RendersStatesDeterministically)
    message(STATUS "[Application Tests] Added: vixen_planet_capture (planet states, repeat-pixel witness)")
endif()
