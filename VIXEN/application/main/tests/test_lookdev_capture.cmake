if(NOT BUILD_TESTS)
    return()
endif()

if(TARGET VixenApp AND TARGET GTest::gtest_main)
    add_executable(vixen_lookdev_capture
        ${CMAKE_CURRENT_LIST_DIR}/test_lookdev_capture.cpp)
    vixen_whole_archive_link_vixen_app(vixen_lookdev_capture PRIVATE)
    target_link_libraries(vixen_lookdev_capture PRIVATE GTest::gtest_main Text)
    target_compile_definitions(vixen_lookdev_capture PRIVATE
        TEXT_LATO_FONT_PATH="${VIXEN_ROOT}/libraries/RenderGraph/assets/ui/LatoLatin-Regular.ttf"
        TEXT_NOTO_ARABIC_FONT_PATH="${VIXEN_ROOT}/libraries/Text/tests/data/NotoSansArabic-VF.ttf")
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

    # Both shaped layouts share one graph and frame so the same mixed-script
    # literal can be compared side by side without a second cold pipeline build.
    set(_glyphsdf_rtl_pair_test "Application.LookdevCapture.GlyphSdf.RtlProof.Pair")
    set(_glyphsdf_rtl_pair_dir "${_glyphsdf_capture_dir}/rtl-proof-pair")
    add_test(NAME "${_glyphsdf_rtl_pair_test}"
        COMMAND "$<TARGET_FILE:vixen_lookdev_capture>"
                "--gtest_filter=LookdevCapture.RendersGlyphSdfLocalizedSign")
    set_tests_properties("${_glyphsdf_rtl_pair_test}" PROPERTIES
        LABELS "Application;GlyphSdfProofPair"
        TIMEOUT 1800
        ENVIRONMENT
            "${_lookdev_test_environment};VIXEN_WINDOW_WIDTH=1920;VIXEN_WINDOW_HEIGHT=1080;VIXEN_GLYPHSDF_SCENE=1;VIXEN_GLYPHSDF_RTL_PROOF=1;VIXEN_GLYPHSDF_VARIANT=rtl-proof-pair;VIXEN_LOOKDEV_CAPTURE_FRAMES=1;VIXEN_LOOKDEV_CAPTURE_BUDGET_MS=900000;VIXEN_LOOKDEV_CAPTURE_DIR=${_glyphsdf_rtl_pair_dir}"
        ENVIRONMENT_MODIFICATION
            "VIXEN_TEST_CELSHADE_LAMBERT_GGX=set:1;VIXEN_DDGI_CORNELL_BAKED_DEMO=unset:;VIXEN_DDGI_CORNELL_VIRTUAL_DEMO=unset:;VIXEN_DDGI_CORNELL_HYBRID_DEMO=unset:;VIXEN_POLICY_STENCIL=unset:;VIXEN_POLICY_STENCIL_TILES=unset:;VIXEN_GLYPHSDF_COMPARE=unset:")
    list(APPEND _glyphsdf_capture_tests "${_glyphsdf_rtl_pair_test}")

    foreach(_resolution IN ITEMS 128 256)
        set(_glyphsdf_compare_test "Application.LookdevCapture.GlyphSdf.AnalyticVsVoxel${_resolution}")
        if(_resolution EQUAL 256)
            set(_glyphsdf_compare_test "Application.LookdevCapture.GlyphSdf.AnalyticVsVoxel")
        endif()
        set(_glyphsdf_compare_dir "${_glyphsdf_capture_dir}/analytic-vs-voxel-${_resolution}")
        add_test(NAME "${_glyphsdf_compare_test}"
            COMMAND "$<TARGET_FILE:vixen_lookdev_capture>"
                    "--gtest_filter=LookdevCapture.RendersGlyphSdfAnalyticVsVoxel")
        set_tests_properties("${_glyphsdf_compare_test}" PROPERTIES
            LABELS "Application;GlyphSdf${_resolution}"
            TIMEOUT 1800
            ENVIRONMENT
                "${_lookdev_test_environment};VIXEN_WINDOW_WIDTH=1920;VIXEN_WINDOW_HEIGHT=1080;VIXEN_GLYPHSDF_SCENE=1;VIXEN_GLYPHSDF_COMPARE=1;VIXEN_GLYPHSDF_VARIANT=latin-carved;VIXEN_GLYPHSDF_BAKE_RESOLUTION=${_resolution};VIXEN_LOOKDEV_CAPTURE_FRAMES=1;VIXEN_LOOKDEV_CAPTURE_BUDGET_MS=900000;VIXEN_LOOKDEV_CAPTURE_DIR=${_glyphsdf_compare_dir};VIXEN_PERF_CSV=${_glyphsdf_compare_dir}/native-perf.csv"
            ENVIRONMENT_MODIFICATION
                "VIXEN_TEST_CELSHADE_LAMBERT_GGX=set:1;VIXEN_DDGI_CORNELL_BAKED_DEMO=unset:;VIXEN_DDGI_CORNELL_VIRTUAL_DEMO=unset:;VIXEN_DDGI_CORNELL_HYBRID_DEMO=unset:;VIXEN_DDGI_CORNELL_MIXED_DEMO=unset:;VIXEN_POLICY_STENCIL=unset:;VIXEN_POLICY_STENCIL_TILES=unset:")
        list(APPEND _glyphsdf_capture_tests "${_glyphsdf_compare_test}")

        set(_glyphsdf_yaw_test "Application.LookdevCapture.GlyphSdf.AnalyticVsVoxel${_resolution}.Yaw30")
        set(_glyphsdf_yaw_dir "${_glyphsdf_capture_dir}/analytic-vs-voxel-${_resolution}-yaw30")
        add_test(NAME "${_glyphsdf_yaw_test}"
            COMMAND "$<TARGET_FILE:vixen_lookdev_capture>"
                    "--gtest_filter=LookdevCapture.RendersGlyphSdfAnalyticVsVoxel")
        set_tests_properties("${_glyphsdf_yaw_test}" PROPERTIES
            LABELS "Application;GlyphSdf${_resolution}Yaw30"
            TIMEOUT 1800
            ENVIRONMENT
                "${_lookdev_test_environment};VIXEN_WINDOW_WIDTH=1920;VIXEN_WINDOW_HEIGHT=1080;VIXEN_GLYPHSDF_SCENE=1;VIXEN_GLYPHSDF_COMPARE=1;VIXEN_GLYPHSDF_VARIANT=latin-carved;VIXEN_GLYPHSDF_BAKE_RESOLUTION=${_resolution};VIXEN_LOOKDEV_CAPTURE_ANGLE=0;VIXEN_GLYPHSDF_CAPTURE_YAW_DEGREES=30;VIXEN_LOOKDEV_CAPTURE_FRAMES=1;VIXEN_LOOKDEV_CAPTURE_BUDGET_MS=900000;VIXEN_LOOKDEV_CAPTURE_DIR=${_glyphsdf_yaw_dir};VIXEN_PERF_CSV=${_glyphsdf_yaw_dir}/native-perf.csv"
            ENVIRONMENT_MODIFICATION
                "VIXEN_TEST_CELSHADE_LAMBERT_GGX=set:1;VIXEN_DDGI_CORNELL_BAKED_DEMO=unset:;VIXEN_DDGI_CORNELL_VIRTUAL_DEMO=unset:;VIXEN_DDGI_CORNELL_HYBRID_DEMO=unset:;VIXEN_DDGI_CORNELL_MIXED_DEMO=unset:;VIXEN_POLICY_STENCIL=unset:;VIXEN_POLICY_STENCIL_TILES=unset:")
        list(APPEND _glyphsdf_capture_tests "${_glyphsdf_yaw_test}")
    endforeach()

    set(_stored_sdf_brickmap_test "Application.LookdevCapture.StoredSdf.Brickmap")
    set(_stored_sdf_brickmap_dir "${_glyphsdf_capture_dir}/nontext-brickmap")
    add_test(NAME "${_stored_sdf_brickmap_test}"
        COMMAND "$<TARGET_FILE:vixen_lookdev_capture>"
                "--gtest_filter=LookdevCapture.RendersFourStatesAtFourAnglesDeterministically")
    set_tests_properties("${_stored_sdf_brickmap_test}" PROPERTIES
        LABELS "Application;StoredSdfBrickmap"
        TIMEOUT 1800
        ENVIRONMENT
            "${_lookdev_test_environment};VIXEN_WINDOW_WIDTH=1920;VIXEN_WINDOW_HEIGHT=1080;VIXEN_BRICKMAP_SCENE=1;VIXEN_LOOKDEV_CAPTURE_STATE=midday;VIXEN_LOOKDEV_CAPTURE_ANGLE=0;VIXEN_LOOKDEV_CAPTURE_FRAMES=1;VIXEN_LOOKDEV_CAPTURE_BUDGET_MS=900000;VIXEN_LOOKDEV_CAPTURE_DIR=${_stored_sdf_brickmap_dir};VIXEN_PERF_CSV=${_stored_sdf_brickmap_dir}/native-perf.csv"
        ENVIRONMENT_MODIFICATION
            "VIXEN_TEST_CELSHADE_LAMBERT_GGX=set:1;VIXEN_DDGI_CORNELL_BAKED_DEMO=unset:;VIXEN_DDGI_CORNELL_VIRTUAL_DEMO=unset:;VIXEN_DDGI_CORNELL_HYBRID_DEMO=unset:;VIXEN_DDGI_CORNELL_MIXED_DEMO=unset:;VIXEN_POLICY_STENCIL=unset:;VIXEN_POLICY_STENCIL_TILES=unset:")

    foreach(_resolution IN ITEMS 128 256)
        foreach(_yaw IN ITEMS 0 30)
            set(_sdf_filter_nontext_test "Application.LookdevCapture.SdfFilter.NonText${_resolution}.Yaw${_yaw}")
            set(_sdf_filter_nontext_dir "${_glyphsdf_capture_dir}/sdf-filter-nontext-${_resolution}-yaw${_yaw}")
            add_test(NAME "${_sdf_filter_nontext_test}"
                COMMAND "$<TARGET_FILE:vixen_lookdev_capture>"
                        "--gtest_filter=LookdevCapture.RendersSdfFilterNonText")
            set_tests_properties("${_sdf_filter_nontext_test}" PROPERTIES
                LABELS "Application;GlyphSdfNonText${_resolution}Yaw${_yaw}"
                TIMEOUT 1200
                ENVIRONMENT
                    "${_lookdev_test_environment};VIXEN_WINDOW_WIDTH=1920;VIXEN_WINDOW_HEIGHT=1080;VIXEN_SDF_FILTER_NON_TEXT=1;VIXEN_SDF_FILTER_NON_TEXT_RESOLUTION=${_resolution};VIXEN_GLYPHSDF_CAPTURE_YAW_DEGREES=${_yaw};VIXEN_LOOKDEV_CAPTURE_STATE=midday;VIXEN_LOOKDEV_CAPTURE_ANGLE=0;VIXEN_LOOKDEV_CAPTURE_FRAMES=1;VIXEN_LOOKDEV_CAPTURE_BUDGET_MS=900000;VIXEN_LOOKDEV_CAPTURE_DIR=${_sdf_filter_nontext_dir};VIXEN_PERF_CSV=${_sdf_filter_nontext_dir}/native-perf.csv"
                ENVIRONMENT_MODIFICATION
                    "VIXEN_TEST_CELSHADE_LAMBERT_GGX=set:1;VIXEN_DDGI_CORNELL_BAKED_DEMO=unset:;VIXEN_DDGI_CORNELL_VIRTUAL_DEMO=unset:;VIXEN_DDGI_CORNELL_HYBRID_DEMO=unset:;VIXEN_DDGI_CORNELL_MIXED_DEMO=unset:;VIXEN_GLYPHSDF_SCENE=unset:;VIXEN_GLYPHSDF_COMPARE=unset:;VIXEN_BRICKMAP_SCENE=unset:;VIXEN_POLICY_STENCIL=unset:;VIXEN_POLICY_STENCIL_TILES=unset:")
            list(APPEND _glyphsdf_capture_tests "${_sdf_filter_nontext_test}")
        endforeach()
    endforeach()
    vixen_lock_gpu_tests(TESTS ${_glyphsdf_capture_tests})
    message(STATUS "[Application Tests] Added: glyph and non-text SDF comparisons at 128^3/256^3 from 0/30 degree yaws")
endif()
