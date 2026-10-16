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
    set(_planet_test_environment_modification
        "VIXEN_TEST_CELSHADE_LAMBERT_GGX=unset:;VIXEN_DDGI_CORNELL_BAKED_DEMO=unset:;VIXEN_DDGI_CORNELL_VIRTUAL_DEMO=unset:;VIXEN_DDGI_CORNELL_HYBRID_DEMO=unset:;VIXEN_DDGI_CORNELL_MIXED_DEMO=unset:")
    # One state per test is the default registration (R565 C1): each is bounded by its own timeout
    # (two fresh processes of 8 frames each, plus device-loss retries), so a stalled state costs the
    # queue one state, not twelve. The state list mirrors Planet::StateNames().
    set(_planet_states C1 C1-epic C2 C2-term C4a C4b C4c C5a-moon C7 C7-60 C7-60-1p5au C9)
    set(_planet_state_tests)
    foreach(_state IN LISTS _planet_states)
        set(_test Application.PlanetCapture.${_state})
        add_test(NAME ${_test}
            COMMAND ${CMAKE_COMMAND}
                "-DPLANET_CAPTURE_EXE=$<TARGET_FILE:vixen_planet_capture>"
                "-DPLANET_CAPTURE_DIR=${CMAKE_BINARY_DIR}/runtime-captures/planet"
                "-DPLANET_CAPTURE_STATES=${_state}"
                -P "${CMAKE_CURRENT_LIST_DIR}/run_planet_capture.cmake")
        set_tests_properties(${_test}
            PROPERTIES
                LABELS "Application"
                TIMEOUT 900
                ENVIRONMENT "${_planet_test_environment}"
                ENVIRONMENT_MODIFICATION "${_planet_test_environment_modification}")
        list(APPEND _planet_state_tests ${_test})
    endforeach()
    vixen_lock_gpu_tests(TESTS ${_planet_state_tests})

    # The 12-state aggregate is native-only (R565 C1): on Dozen it holds the GPU for an hour and
    # stalls; natively it takes minutes. It carries only the `native` label, which the split runner
    # excludes by default (`ctest -L native` selects it), and twelve per-state timeouts.
    list(LENGTH _planet_states _planet_state_count)
    math(EXPR _planet_aggregate_timeout "${_planet_state_count} * 900")
    add_test(NAME Application.PlanetCapture.RendersStatesDeterministically
        COMMAND ${CMAKE_COMMAND}
            "-DPLANET_CAPTURE_EXE=$<TARGET_FILE:vixen_planet_capture>"
            "-DPLANET_CAPTURE_DIR=${CMAKE_BINARY_DIR}/runtime-captures/planet"
            -P "${CMAKE_CURRENT_LIST_DIR}/run_planet_capture.cmake")
    set_tests_properties(Application.PlanetCapture.RendersStatesDeterministically
        PROPERTIES
            LABELS "native"
            TIMEOUT ${_planet_aggregate_timeout}
            ENVIRONMENT "${_planet_test_environment}"
            ENVIRONMENT_MODIFICATION "${_planet_test_environment_modification}")
    vixen_lock_gpu_tests(TESTS Application.PlanetCapture.RendersStatesDeterministically)
    message(STATUS "[Application Tests] Added: vixen_planet_capture (${_planet_state_count} per-state tests, repeat-pixel witness; aggregate under label native)")
endif()
