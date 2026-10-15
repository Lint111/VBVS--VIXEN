if(NOT BUILD_TESTS)
    return()
endif()

if(TARGET VixenApp AND TARGET GTest::gtest_main)
    add_executable(vixen_lookdev_capture
        ${CMAKE_CURRENT_LIST_DIR}/test_lookdev_capture.cpp)
    vixen_whole_archive_link_vixen_app(vixen_lookdev_capture PRIVATE)
    target_link_libraries(vixen_lookdev_capture PRIVATE GTest::gtest_main Text)
    target_compile_definitions(vixen_lookdev_capture PRIVATE
        TEXT_LATO_FONT_PATH="${VIXEN_ROOT}/libraries/RenderGraph/assets/ui/LatoLatin-Regular.ttf")
    set_target_properties(vixen_lookdev_capture PROPERTIES FOLDER "Tests/Application")
    set(_lookdev_test_environment
        ${VIXEN_CTEST_GPU_ENVIRONMENT}
        "VIXEN_HDR_EXPOSURE=1"
        "VIXEN_HUD_SUPPRESS=1")
    add_test(NAME Application.LookdevCapture.RendersFourStatesAtFourAnglesDeterministically
        COMMAND ${CMAKE_COMMAND}
            "-DLOOKDEV_CAPTURE_EXE=$<TARGET_FILE:vixen_lookdev_capture>"
            "-DLOOKDEV_CAPTURE_DIR=${CMAKE_BINARY_DIR}/runtime-captures/lookdev"
            -DLOOKDEV_CAPTURE_BUDGET_S=900
            -P "${CMAKE_CURRENT_LIST_DIR}/run_lookdev_capture.cmake")
    set_tests_properties(Application.LookdevCapture.RendersFourStatesAtFourAnglesDeterministically
        PROPERTIES
            LABELS "Application"
            TIMEOUT 1200
            ENVIRONMENT "${_lookdev_test_environment}"
            ENVIRONMENT_MODIFICATION
                "VIXEN_TEST_CELSHADE_LAMBERT_GGX=unset:;VIXEN_DDGI_CORNELL_BAKED_DEMO=unset:;VIXEN_DDGI_CORNELL_VIRTUAL_DEMO=unset:;VIXEN_DDGI_CORNELL_HYBRID_DEMO=unset:;VIXEN_DDGI_CORNELL_MIXED_DEMO=unset:")
    vixen_lock_gpu_tests(TESTS Application.LookdevCapture.RendersFourStatesAtFourAnglesDeterministically)
    message(STATUS "[Application Tests] Added: vixen_lookdev_capture (16 states/angles, repeat-pixel witness)")

    # Native S2 parity jobs capture one state/angle/mode per CTest case. Keeping
    # each GPU capture as its own test lets the box queue admit it independently;
    # the lane compares each reference/system pair after both jobs finish.
    set(_exposure_parity_tests)
    foreach(_state IN ITEMS midday late-afternoon overcast night-service)
        foreach(_angle RANGE 0 3)
            foreach(_mode IN ITEMS reference system)
                set(_test "Application.ExposureTonemapParity.${_mode}.${_state}.angle${_angle}")
                set(_capture_dir "${CMAKE_BINARY_DIR}/runtime-captures/exposure-parity/${_state}-angle-${_angle}/${_mode}")
                set(_mode_flag "0")
                if(_mode STREQUAL "reference")
                    set(_mode_flag "1")
                endif()
                add_test(NAME "${_test}"
                    COMMAND "$<TARGET_FILE:vixen_lookdev_capture>"
                            "--gtest_filter=LookdevCapture.RendersFourStatesAtFourAnglesDeterministically")
                set_tests_properties("${_test}" PROPERTIES
                    LABELS "Application"
                    TIMEOUT 900
                    ENVIRONMENT
                        "${_lookdev_test_environment};VIXEN_LOOKDEV_CAPTURE_STATE=${_state};VIXEN_LOOKDEV_CAPTURE_ANGLE=${_angle};VIXEN_LOOKDEV_CAPTURE_FRAMES=1;VIXEN_LOOKDEV_CAPTURE_DIR=${_capture_dir};VIXEN_EXPOSURE_TONEMAP_REFERENCE=${_mode_flag}"
                    ENVIRONMENT_MODIFICATION
                        "VIXEN_TEST_CELSHADE_LAMBERT_GGX=unset:;VIXEN_DDGI_CORNELL_BAKED_DEMO=unset:;VIXEN_DDGI_CORNELL_VIRTUAL_DEMO=unset:;VIXEN_DDGI_CORNELL_HYBRID_DEMO=unset:;VIXEN_DDGI_CORNELL_MIXED_DEMO=unset:")
                list(APPEND _exposure_parity_tests "${_test}")
            endforeach()
        endforeach()
    endforeach()
    vixen_lock_gpu_tests(TESTS ${_exposure_parity_tests})
    message(STATUS "[Application Tests] Added: 32 one-frame native tone-map parity captures")

    set(_glyphsdf_capture_dir "${CMAKE_BINARY_DIR}/runtime-captures/glyphsdf")
    set(_glyphsdf_capture_tests)
    foreach(_variant IN ITEMS latin-carved latin-embossed qps-rtl-carved qps-rtl-embossed)
        set(_test "Application.LookdevCapture.GlyphSdf.${_variant}")
        set(_capture_dir "${_glyphsdf_capture_dir}/${_variant}")
        add_test(NAME "${_test}"
            COMMAND "$<TARGET_FILE:vixen_lookdev_capture>"
                    "--gtest_filter=LookdevCapture.RendersGlyphSdfLocalizedSign")
        set_tests_properties("${_test}" PROPERTIES
            LABELS "Application"
            TIMEOUT 1200
            ENVIRONMENT
                "${_lookdev_test_environment};VIXEN_WINDOW_WIDTH=1920;VIXEN_WINDOW_HEIGHT=1080;VIXEN_GLYPHSDF_SCENE=1;VIXEN_GLYPHSDF_VARIANT=${_variant};VIXEN_LOOKDEV_CAPTURE_FRAMES=1;VIXEN_LOOKDEV_CAPTURE_BUDGET_MS=900000;VIXEN_LOOKDEV_CAPTURE_DIR=${_capture_dir}"
            ENVIRONMENT_MODIFICATION
                "VIXEN_TEST_CELSHADE_LAMBERT_GGX=set:1;VIXEN_DDGI_CORNELL_BAKED_DEMO=unset:;VIXEN_DDGI_CORNELL_VIRTUAL_DEMO=unset:;VIXEN_DDGI_CORNELL_HYBRID_DEMO=unset:;VIXEN_DDGI_CORNELL_MIXED_DEMO=unset:;VIXEN_POLICY_STENCIL=unset:;VIXEN_POLICY_STENCIL_TILES=unset:;VIXEN_GLYPHSDF_COMPARE=unset:")
        list(APPEND _glyphsdf_capture_tests "${_test}")
    endforeach()

    set(_glyphsdf_compare_test "Application.LookdevCapture.GlyphSdf.AnalyticVsVoxel")
    set(_glyphsdf_compare_dir "${_glyphsdf_capture_dir}/analytic-vs-voxel")
    add_test(NAME "${_glyphsdf_compare_test}"
        COMMAND "$<TARGET_FILE:vixen_lookdev_capture>"
                "--gtest_filter=LookdevCapture.RendersGlyphSdfAnalyticVsVoxel")
    set_tests_properties("${_glyphsdf_compare_test}" PROPERTIES
        LABELS "Application"
        TIMEOUT 1800
        ENVIRONMENT
            "${_lookdev_test_environment};VIXEN_WINDOW_WIDTH=1920;VIXEN_WINDOW_HEIGHT=1080;VIXEN_GLYPHSDF_SCENE=1;VIXEN_GLYPHSDF_COMPARE=1;VIXEN_GLYPHSDF_VARIANT=latin-carved;VIXEN_LOOKDEV_CAPTURE_FRAMES=1;VIXEN_LOOKDEV_CAPTURE_BUDGET_MS=900000;VIXEN_LOOKDEV_CAPTURE_DIR=${_glyphsdf_compare_dir}"
        ENVIRONMENT_MODIFICATION
            "VIXEN_TEST_CELSHADE_LAMBERT_GGX=set:1;VIXEN_DDGI_CORNELL_BAKED_DEMO=unset:;VIXEN_DDGI_CORNELL_VIRTUAL_DEMO=unset:;VIXEN_DDGI_CORNELL_HYBRID_DEMO=unset:;VIXEN_DDGI_CORNELL_MIXED_DEMO=unset:;VIXEN_POLICY_STENCIL=unset:;VIXEN_POLICY_STENCIL_TILES=unset:")
    list(APPEND _glyphsdf_capture_tests "${_glyphsdf_compare_test}")
    vixen_lock_gpu_tests(TESTS ${_glyphsdf_capture_tests})
    message(STATUS "[Application Tests] Added: four lit GlyphOutline frames and 128^3 analytic/voxel comparison")
endif()
