if(NOT BUILD_TESTS)
    return()
endif()

if(TARGET VixenApp AND TARGET GTest::gtest_main)
    add_executable(test_cel_shading_graph
        ${CMAKE_CURRENT_LIST_DIR}/test_cel_shading_graph.cpp)
    vixen_whole_archive_link_vixen_app(test_cel_shading_graph PRIVATE)
    target_link_libraries(test_cel_shading_graph PRIVATE GTest::gtest_main stb)
    set_target_properties(test_cel_shading_graph PROPERTIES FOLDER "Tests/Application")

    set(_celshade_binary "$<TARGET_FILE:test_cel_shading_graph>")
    set(_celshade_capture_dir "${CMAKE_BINARY_DIR}/celshade-captures")
    file(MAKE_DIRECTORY "${_celshade_capture_dir}")
    set(_celshade_env ${VIXEN_CTEST_GPU_ENVIRONMENT})

    add_test(NAME CelShading.BandQuantization
        COMMAND ${_celshade_binary} --gtest_filter=CelShading.BandQuantizationMapsNdotLToExpectedBand)
    set_tests_properties(CelShading.BandQuantization PROPERTIES LABELS "Application")

    foreach(_band_count 2 3 5)
        add_test(NAME CelShading.CaptureStarsBands${_band_count}
            COMMAND ${_celshade_binary} --gtest_filter=CelShading.CaptureBandCount)
        set_tests_properties(CelShading.CaptureStarsBands${_band_count} PROPERTIES
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:VIXEN>"
            ENVIRONMENT "${_celshade_env};VIXEN_CELSHADE_CAPTURE_PREFIX=${_celshade_capture_dir}/stars-${_band_count};VIXEN_CELSHADE_BAND_COUNT=${_band_count}"
            ENVIRONMENT_MODIFICATION "VIXEN_STARLIGHT_DEMO=set:1"
            LABELS "Application")
    endforeach()

    add_test(NAME CelShading.CaptureCornell
        COMMAND ${_celshade_binary} --gtest_filter=CelShading.CaptureBandCount)
    set_tests_properties(CelShading.CaptureCornell PROPERTIES
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:VIXEN>"
        ENVIRONMENT "${_celshade_env};VIXEN_CELSHADE_CAPTURE_PREFIX=${_celshade_capture_dir}/cornell;VIXEN_CELSHADE_BAND_COUNT=3"
        ENVIRONMENT_MODIFICATION "VIXEN_DDGI_CORNELL_VIRTUAL_DEMO=set:1"
        LABELS "Application")

    add_test(NAME CelShading.ModeSwitch
        COMMAND ${_celshade_binary} --gtest_filter=CelShading.ModeSwitchTakesEffectOnTheLiveNode)
    set_tests_properties(CelShading.ModeSwitch PROPERTIES
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:VIXEN>"
        ENVIRONMENT "${_celshade_env};VIXEN_CELSHADE_CAPTURE_PREFIX=${_celshade_capture_dir}/mode-switch"
        ENVIRONMENT_MODIFICATION "VIXEN_STARLIGHT_DEMO=set:1"
        LABELS "Application")

    add_test(NAME CelShading.ParameterBounds
        COMMAND ${_celshade_binary} --gtest_filter=CelShading.ParameterBoundsRenderWithoutThrowing)
    set_tests_properties(CelShading.ParameterBounds PROPERTIES
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:VIXEN>"
        ENVIRONMENT "${_celshade_env}"
        ENVIRONMENT_MODIFICATION "VIXEN_STARLIGHT_DEMO=set:1"
        LABELS "Application")

    vixen_lock_gpu_tests(TESTS
        CelShading.CaptureStarsBands2
        CelShading.CaptureStarsBands3
        CelShading.CaptureStarsBands5
        CelShading.CaptureCornell
        CelShading.ModeSwitch
        CelShading.ParameterBounds)

    message(STATUS "[Application Tests] Added: test_cel_shading_graph (cel ramp, live mode switching, and scene captures)")
endif()
