if(NOT BUILD_TESTS)
    return()
endif()

if(TARGET VixenApp AND TARGET GTest::gtest_main)
    add_executable(vixen_lookdev_capture
        ${CMAKE_CURRENT_LIST_DIR}/test_lookdev_capture.cpp)
    vixen_whole_archive_link_vixen_app(vixen_lookdev_capture PRIVATE)
    target_link_libraries(vixen_lookdev_capture PRIVATE GTest::gtest_main Text stb)
    target_compile_definitions(vixen_lookdev_capture PRIVATE
        TEXT_LATO_FONT_PATH="${VIXEN_ROOT}/libraries/RenderGraph/assets/ui/LatoLatin-Regular.ttf"
        TEXT_NOTO_ARABIC_FONT_PATH="${VIXEN_ROOT}/libraries/Text/tests/data/NotoSansArabic-VF.ttf")
    set_target_properties(vixen_lookdev_capture PROPERTIES FOLDER "Tests/Application")
    set(_lookdev_test_environment
        ${VIXEN_CTEST_GPU_ENVIRONMENT}
        "VIXEN_HDR_EXPOSURE=1"
        "VIXEN_HUD_SUPPRESS=1")
    set(_lookdev_test_environment_modification
        "VIXEN_TEST_CELSHADE_LAMBERT_GGX=unset:;VIXEN_DDGI_CORNELL_BAKED_DEMO=unset:;VIXEN_DDGI_CORNELL_VIRTUAL_DEMO=unset:;VIXEN_DDGI_CORNELL_HYBRID_DEMO=unset:;VIXEN_DDGI_CORNELL_MIXED_DEMO=unset:")
    # One state per test is the default registration: a state is two fresh processes (about 10 minutes
    # on Dozen), so the four-state run exceeds any single timeout there. Each state keeps the 1200 s
    # bound on its own, and a stalled state costs the queue one state, not four.
    set(_lookdev_states midday late-afternoon overcast night-service)
    set(_lookdev_state_tests)
    foreach(_state IN LISTS _lookdev_states)
        set(_test Application.LookdevCapture.State.${_state})
        add_test(NAME ${_test}
            COMMAND ${CMAKE_COMMAND}
                "-DLOOKDEV_CAPTURE_EXE=$<TARGET_FILE:vixen_lookdev_capture>"
                "-DLOOKDEV_CAPTURE_DIR=${CMAKE_BINARY_DIR}/runtime-captures/lookdev"
                "-DLOOKDEV_CAPTURE_STATES=${_state}"
                -DLOOKDEV_CAPTURE_BUDGET_S=900
                -P "${CMAKE_CURRENT_LIST_DIR}/run_lookdev_capture.cmake")
        set_tests_properties(${_test}
            PROPERTIES
                LABELS "Application"
                TIMEOUT 1200
                ENVIRONMENT "${_lookdev_test_environment}"
                ENVIRONMENT_MODIFICATION "${_lookdev_test_environment_modification}")
        list(APPEND _lookdev_state_tests ${_test})
    endforeach()
    vixen_lock_gpu_tests(TESTS ${_lookdev_state_tests})

    # R676.2: the look-review set is the presentation frame (jittered samples resolved by the
    # anti-aliasing module, `taa` by default), not the canonical one-sample frame above. Same
    # states, angles and first/repeat byte-identity witness; its own output directory.
    set(_lookdev_presentation_tests)
    foreach(_state IN LISTS _lookdev_states)
        set(_test Application.LookdevCapture.Presentation.${_state})
        add_test(NAME ${_test}
            COMMAND ${CMAKE_COMMAND}
                "-DLOOKDEV_CAPTURE_EXE=$<TARGET_FILE:vixen_lookdev_capture>"
                "-DLOOKDEV_CAPTURE_DIR=${CMAKE_BINARY_DIR}/runtime-captures/lookdev-presentation"
                "-DLOOKDEV_CAPTURE_STATES=${_state}"
                -DLOOKDEV_CAPTURE_BUDGET_S=900
                -P "${CMAKE_CURRENT_LIST_DIR}/run_lookdev_capture.cmake")
        set_tests_properties(${_test}
            PROPERTIES
                LABELS "Application"
                TIMEOUT 1200
                ENVIRONMENT "${_lookdev_test_environment};VIXEN_LOOKDEV_CAPTURE_SAMPLING=presentation"
                ENVIRONMENT_MODIFICATION "${_lookdev_test_environment_modification}")
        list(APPEND _lookdev_presentation_tests ${_test})
    endforeach()
    vixen_lock_gpu_tests(TESTS ${_lookdev_presentation_tests})

    # The four-state aggregate keeps its name for the capture tooling that selects it. It carries only
    # the `native` label, which the split runner excludes by default (`ctest -L native` selects it).
    list(LENGTH _lookdev_states _lookdev_state_count)
    math(EXPR _lookdev_aggregate_timeout "${_lookdev_state_count} * 1200")
    add_test(NAME Application.LookdevCapture.RendersFourStatesAtFourAnglesDeterministically
        COMMAND ${CMAKE_COMMAND}
            "-DLOOKDEV_CAPTURE_EXE=$<TARGET_FILE:vixen_lookdev_capture>"
            "-DLOOKDEV_CAPTURE_DIR=${CMAKE_BINARY_DIR}/runtime-captures/lookdev"
            -DLOOKDEV_CAPTURE_BUDGET_S=900
            -P "${CMAKE_CURRENT_LIST_DIR}/run_lookdev_capture.cmake")
    set_tests_properties(Application.LookdevCapture.RendersFourStatesAtFourAnglesDeterministically
        PROPERTIES
            LABELS "native"
            TIMEOUT ${_lookdev_aggregate_timeout}
            ENVIRONMENT "${_lookdev_test_environment}"
            ENVIRONMENT_MODIFICATION "${_lookdev_test_environment_modification}")
    vixen_lock_gpu_tests(TESTS Application.LookdevCapture.RendersFourStatesAtFourAnglesDeterministically)
    message(STATUS "[Application Tests] Added: vixen_lookdev_capture (${_lookdev_state_count} per-state tests of 4 angles, repeat-pixel witness; aggregate under label native)")

    foreach(_depth_case IN ITEMS native reduced-odd fixed-exposure)
        set(_depth_scale 1.0)
        set(_depth_fixed_exposure 0)
        if(_depth_case STREQUAL "reduced-odd")
            set(_depth_scale 0.5)
        endif()
        if(_depth_case STREQUAL "fixed-exposure")
            set(_depth_fixed_exposure 1)
        endif()
        set(_depth_test "Application.ReconstructionInputs.Depth.${_depth_case}")
        set(_depth_dir "${CMAKE_BINARY_DIR}/runtime-captures/reconstruction/${_depth_case}")
        add_test(NAME "${_depth_test}" COMMAND "$<TARGET_FILE:vixen_lookdev_capture>"
            "--gtest_filter=LookdevCapture.RendersFourStatesAtFourAnglesDeterministically"
            "--render-scale=${_depth_scale}" "--upscaler=none")
        set_tests_properties("${_depth_test}" PROPERTIES LABELS "Application" TIMEOUT 900
            ENVIRONMENT "${_lookdev_test_environment};VIXEN_RECONSTRUCTION_DEPTH=1;VIXEN_LOOKDEV_DEPTH_READBACK=1;VIXEN_LOOKDEV_READBACK=1;VIXEN_LOOKDEV_CAPTURE_STATE=midday;VIXEN_LOOKDEV_CAPTURE_ANGLE=0;VIXEN_LOOKDEV_CAPTURE_FRAMES=4;VIXEN_WINDOW_WIDTH=501;VIXEN_WINDOW_HEIGHT=333;VIXEN_LOOKDEV_CAPTURE_DIR=${_depth_dir};VIXEN_PERF_CSV=${_depth_dir}/perf.csv;VIXEN_HDR_EXPOSURE_FIXED=${_depth_fixed_exposure}"
            ENVIRONMENT_MODIFICATION "VIXEN_TEST_CELSHADE_LAMBERT_GGX=unset:;VIXEN_DDGI_CORNELL_BAKED_DEMO=unset:;VIXEN_DDGI_CORNELL_VIRTUAL_DEMO=unset:;VIXEN_DDGI_CORNELL_HYBRID_DEMO=unset:;VIXEN_DDGI_CORNELL_MIXED_DEMO=unset:")
        vixen_lock_gpu_tests(TESTS "${_depth_test}")
    endforeach()

    set(_temporal_test "Application.ReconstructionInputs.TemporalLifecycle")
    set(_temporal_dir "${CMAKE_BINARY_DIR}/runtime-captures/reconstruction/temporal-lifecycle")
    add_test(NAME "${_temporal_test}" COMMAND "$<TARGET_FILE:vixen_lookdev_capture>"
        "--gtest_filter=LookdevCapture.RendersFourStatesAtFourAnglesDeterministically"
        "--render-scale=0.5" "--upscaler=none")
    set_tests_properties("${_temporal_test}" PROPERTIES LABELS "Application" TIMEOUT 900
        ENVIRONMENT "${_lookdev_test_environment};VIXEN_RECONSTRUCTION_DEPTH=1;VIXEN_LOOKDEV_DEPTH_READBACK=1;VIXEN_LOOKDEV_READBACK=1;VIXEN_LOOKDEV_MOTION_PROBE=1;VIXEN_LOOKDEV_HISTORY_PROBE=1;VIXEN_LOOKDEV_CAPTURE_STATE=midday;VIXEN_LOOKDEV_CAPTURE_ANGLE=0;VIXEN_LOOKDEV_CAPTURE_FRAMES=4;VIXEN_WINDOW_WIDTH=501;VIXEN_WINDOW_HEIGHT=333;VIXEN_LOOKDEV_CAPTURE_DIR=${_temporal_dir};VIXEN_HDR_EXPOSURE_FIXED=1")
    vixen_lock_gpu_tests(TESTS "${_temporal_test}")

    # T-2883: one look-dev state under the Khronos validation layer. Fails when a
    # descriptor or dispatch uses a null ray-query TLAS (only reachable on a device
    # with RayQueryLighting; Dozen compiles the binding out).
    set(_validation_layer_path "${VIXEN_VULKAN_LAYER_PATH}")
    if(WIN32 AND NOT _validation_layer_path AND DEFINED ENV{VULKAN_SDK})
        set(_validation_layer_path "$ENV{VULKAN_SDK}/Bin")
    endif()
    if(_validation_layer_path)
        set(_validation_test "Application.LookdevCapture.NoNullAccelerationStructure")
        set(_validation_dir "${CMAKE_BINARY_DIR}/runtime-captures/lookdev-validation")
        add_test(NAME "${_validation_test}" COMMAND "$<TARGET_FILE:vixen_lookdev_capture>"
            "--gtest_filter=LookdevCapture.RendersFourStatesAtFourAnglesDeterministically"
            "--render-scale=1.0" "--upscaler=none")
        set_tests_properties("${_validation_test}" PROPERTIES LABELS "Application" TIMEOUT 900
            ENVIRONMENT "${_lookdev_test_environment};VK_LAYER_PATH=${_validation_layer_path};VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation;VIXEN_LOOKDEV_CAPTURE_STATE=midday;VIXEN_LOOKDEV_CAPTURE_ANGLE=0;VIXEN_LOOKDEV_CAPTURE_FRAMES=4;VIXEN_WINDOW_WIDTH=500;VIXEN_WINDOW_HEIGHT=500;VIXEN_LOOKDEV_CAPTURE_DIR=${_validation_dir};VIXEN_HDR_EXPOSURE_FIXED=1"
            FAIL_REGULAR_EXPRESSION "pAccelerationStructures-03580;acceleration structure descriptor .* is using acceleration structure VkAccelerationStructureKHR 0x0")
        vixen_lock_gpu_tests(TESTS "${_validation_test}")

        # Every ring image the march binds must already be in its descriptor's layout:
        # the first frame of each of the four images is where an undeclared binding fails.
        set(_layout_test "Application.LookdevCapture.StorageImageLayoutsMatchDescriptors")
        add_test(NAME "${_layout_test}" COMMAND "$<TARGET_FILE:vixen_lookdev_capture>"
            "--gtest_filter=LookdevCapture.RendersFourStatesAtFourAnglesDeterministically"
            "--render-scale=1.0" "--upscaler=none")
        set_tests_properties("${_layout_test}" PROPERTIES LABELS "Application" TIMEOUT 900
            ENVIRONMENT "${_lookdev_test_environment};VK_LAYER_PATH=${_validation_layer_path};VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation;VIXEN_LOOKDEV_CAPTURE_STATE=midday;VIXEN_LOOKDEV_CAPTURE_ANGLE=0;VIXEN_LOOKDEV_CAPTURE_FRAMES=8;VIXEN_WINDOW_WIDTH=500;VIXEN_WINDOW_HEIGHT=500;VIXEN_LOOKDEV_CAPTURE_DIR=${_validation_dir}-layouts;VIXEN_HDR_EXPOSURE_FIXED=1"
            FAIL_REGULAR_EXPRESSION "None-09600")
        vixen_lock_gpu_tests(TESTS "${_layout_test}")
    endif()

    foreach(_jitter_scale IN ITEMS 1.0 0.5)
        set(_jitter_test "Application.ReconstructionInputs.Jitter.${_jitter_scale}")
        set(_jitter_dir "${CMAKE_BINARY_DIR}/runtime-captures/reconstruction/jitter-${_jitter_scale}")
        add_test(NAME "${_jitter_test}" COMMAND "$<TARGET_FILE:vixen_lookdev_capture>"
            "--gtest_filter=LookdevCapture.RendersFourStatesAtFourAnglesDeterministically"
            "--render-scale=${_jitter_scale}" "--upscaler=none")
        set_tests_properties("${_jitter_test}" PROPERTIES LABELS "Application" TIMEOUT 900
            ENVIRONMENT "${_lookdev_test_environment};VIXEN_RECONSTRUCTION_DEPTH=1;VIXEN_LOOKDEV_DEPTH_READBACK=1;VIXEN_LOOKDEV_READBACK=1;VIXEN_LOOKDEV_JITTER_PROBE=1;VIXEN_LOOKDEV_CAPTURE_STATE=midday;VIXEN_LOOKDEV_CAPTURE_ANGLE=0;VIXEN_LOOKDEV_CAPTURE_FRAMES=4;VIXEN_WINDOW_WIDTH=501;VIXEN_WINDOW_HEIGHT=333;VIXEN_LOOKDEV_CAPTURE_DIR=${_jitter_dir};VIXEN_HDR_EXPOSURE_FIXED=1")
        vixen_lock_gpu_tests(TESTS "${_jitter_test}")
    endforeach()

    # Native S2 parity jobs capture one state/angle/mode per CTest case. Keeping
    # each GPU capture as its own test lets the box queue admit it independently;
    # the lane compares each reference/system pair after both jobs finish.
    set(_exposure_parity_tests)
    foreach(_state IN ITEMS midday late-afternoon overcast night-service)
        foreach(_angle RANGE 0 3)
            foreach(_mode IN ITEMS reference system)
                set(_test "Application.ExposureTonemapParity.${_mode}.${_state}.angle${_angle}")
                set(_capture_dir "${CMAKE_BINARY_DIR}/runtime-captures/exposure-parity/${_state}-angle-${_angle}/${_mode}")
                set(_mode_environment "")
                if(_mode STREQUAL "reference")
                    set(_mode_environment ";VIXEN_EXPOSURE_TONEMAP_REFERENCE=1")
                endif()
                add_test(NAME "${_test}"
                    COMMAND "$<TARGET_FILE:vixen_lookdev_capture>"
                            "--gtest_filter=LookdevCapture.RendersFourStatesAtFourAnglesDeterministically")
                set_tests_properties("${_test}" PROPERTIES
                    LABELS "Application"
                    TIMEOUT 900
                    ENVIRONMENT
                        "${_lookdev_test_environment};VIXEN_LOOKDEV_CAPTURE_STATE=${_state};VIXEN_LOOKDEV_CAPTURE_ANGLE=${_angle};VIXEN_LOOKDEV_CAPTURE_FRAMES=1;VIXEN_LOOKDEV_CAPTURE_DIR=${_capture_dir}${_mode_environment}"
                    ENVIRONMENT_MODIFICATION
                        "VIXEN_TEST_CELSHADE_LAMBERT_GGX=unset:;VIXEN_DDGI_CORNELL_BAKED_DEMO=unset:;VIXEN_DDGI_CORNELL_VIRTUAL_DEMO=unset:;VIXEN_DDGI_CORNELL_HYBRID_DEMO=unset:;VIXEN_DDGI_CORNELL_MIXED_DEMO=unset:")
                list(APPEND _exposure_parity_tests "${_test}")
            endforeach()
        endforeach()
    endforeach()
    vixen_lock_gpu_tests(TESTS ${_exposure_parity_tests})
    message(STATUS "[Application Tests] Added: 32 one-frame native tone-map parity captures")

    # Saturation channel: one midday frame at saturation 1, 0 and 2 in one process (the tone-map stage), then the
    # system tone map and its reference shader are captured separately so the lane can compare the pair.
    foreach(_mode IN ITEMS system reference)
        set(_test "Application.LookdevCapture.SaturationChannel.${_mode}")
        set(_mode_environment "")
        if(_mode STREQUAL "reference")
            set(_mode_environment ";VIXEN_EXPOSURE_TONEMAP_REFERENCE=1")
        endif()
        add_test(NAME "${_test}"
            COMMAND "$<TARGET_FILE:vixen_lookdev_capture>" "--gtest_filter=LookdevCapture.SaturationChannel")
        set_tests_properties("${_test}" PROPERTIES
            LABELS "Application"
            TIMEOUT 900
            ENVIRONMENT
                "${_lookdev_test_environment};VIXEN_LOOKDEV_CAPTURE_STATE=midday;VIXEN_LOOKDEV_CAPTURE_ANGLE=0;VIXEN_LOOKDEV_CAPTURE_FRAMES=4;VIXEN_LOOKDEV_CAPTURE_DIR=${CMAKE_BINARY_DIR}/runtime-captures/saturation/${_mode}${_mode_environment}"
            ENVIRONMENT_MODIFICATION
                "VIXEN_TEST_CELSHADE_LAMBERT_GGX=unset:;VIXEN_DDGI_CORNELL_BAKED_DEMO=unset:;VIXEN_DDGI_CORNELL_VIRTUAL_DEMO=unset:;VIXEN_DDGI_CORNELL_HYBRID_DEMO=unset:;VIXEN_DDGI_CORNELL_MIXED_DEMO=unset:")
        vixen_lock_gpu_tests(TESTS "${_test}")
    endforeach()

    # Owner exposure acceptance scenes: a closed room beside the stage, seen from outside through
    # its doorway, from inside through its window, and on a walk in and back out. The adaptation
    # step is pinned to 0.1 s so 5 frames are 0.5 s of simulated time per walk capture.
    set(_exposure_room_tests)
    foreach(_room IN ITEMS door window walk)
        set(_frames 30)
        if(_room STREQUAL "walk")
            set(_frames 5)
        endif()
        set(_test "Application.LookdevCapture.ExposureRoom.${_room}")
        add_test(NAME "${_test}"
            COMMAND "$<TARGET_FILE:vixen_lookdev_capture>"
                    "--gtest_filter=LookdevCapture.RendersExposureRoomAcceptance")
        set_tests_properties("${_test}" PROPERTIES
            LABELS "Application"
            TIMEOUT 1200
            ENVIRONMENT
                "${_lookdev_test_environment};VIXEN_LOOKDEV_EXPOSURE_ROOM=${_room};VIXEN_HDR_EXPOSURE_DT=0.1;VIXEN_LOOKDEV_CAPTURE_FRAMES=${_frames};VIXEN_WINDOW_WIDTH=480;VIXEN_WINDOW_HEIGHT=320;VIXEN_HDR_METER_DEBUG=200;VIXEN_LOOKDEV_CAPTURE_BUDGET_MS=900000;VIXEN_LOOKDEV_CAPTURE_DIR=${CMAKE_BINARY_DIR}/runtime-captures/exposure-room/${_room}"
            ENVIRONMENT_MODIFICATION
                "VIXEN_TEST_CELSHADE_LAMBERT_GGX=unset:;VIXEN_DDGI_CORNELL_BAKED_DEMO=unset:;VIXEN_DDGI_CORNELL_VIRTUAL_DEMO=unset:;VIXEN_DDGI_CORNELL_HYBRID_DEMO=unset:;VIXEN_DDGI_CORNELL_MIXED_DEMO=unset:")
        list(APPEND _exposure_room_tests "${_test}")
    endforeach()
    vixen_lock_gpu_tests(TESTS ${_exposure_room_tests})

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
                "${_lookdev_test_environment};VIXEN_WINDOW_WIDTH=1920;VIXEN_WINDOW_HEIGHT=1080;VIXEN_GLYPHSDF_SCENE=1;VIXEN_GLYPHSDF_VARIANT=${_variant};VIXEN_GLYPHSDF_TEXT_INVARIANT=1;VIXEN_LOOKDEV_CAPTURE_FRAMES=1;VIXEN_LOOKDEV_CAPTURE_BUDGET_MS=900000;VIXEN_LOOKDEV_CAPTURE_DIR=${_capture_dir}"
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
            "${_lookdev_test_environment};VIXEN_WINDOW_WIDTH=1920;VIXEN_WINDOW_HEIGHT=1080;VIXEN_GLYPHSDF_SCENE=1;VIXEN_GLYPHSDF_RTL_PROOF=1;VIXEN_GLYPHSDF_VARIANT=rtl-proof-pair;VIXEN_GLYPHSDF_TEXT_INVARIANT=1;VIXEN_LOOKDEV_CAPTURE_FRAMES=1;VIXEN_LOOKDEV_CAPTURE_BUDGET_MS=900000;VIXEN_LOOKDEV_CAPTURE_DIR=${_glyphsdf_rtl_pair_dir}"
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
            LABELS "Application;GlyphSdfText${_resolution}Yaw0"
            TIMEOUT 1800
            ENVIRONMENT
                "${_lookdev_test_environment};VIXEN_WINDOW_WIDTH=1920;VIXEN_WINDOW_HEIGHT=1080;VIXEN_GLYPHSDF_SCENE=1;VIXEN_GLYPHSDF_COMPARE=1;VIXEN_GLYPHSDF_VARIANT=latin-carved;VIXEN_GLYPHSDF_BAKE_RESOLUTION=${_resolution};VIXEN_LOOKDEV_CAPTURE_FRAMES=1;VIXEN_LOOKDEV_CAPTURE_BUDGET_MS=900000;VIXEN_LOOKDEV_CAPTURE_DIR=${_glyphsdf_compare_dir};VIXEN_PERF_CSV=${_glyphsdf_compare_dir}/native-perf.csv"
            ENVIRONMENT_MODIFICATION
                "VIXEN_TEST_CELSHADE_LAMBERT_GGX=set:1;VIXEN_DDGI_CORNELL_BAKED_DEMO=unset:;VIXEN_DDGI_CORNELL_VIRTUAL_DEMO=unset:;VIXEN_DDGI_CORNELL_HYBRID_DEMO=unset:;VIXEN_DDGI_CORNELL_MIXED_DEMO=unset:;VIXEN_POLICY_STENCIL=unset:;VIXEN_POLICY_STENCIL_TILES=unset:;VIXEN_SHADOW_CONFIG_ENABLED=unset:;VIXEN_SHADOW_CONFIG_BIAS_EPSILON=unset:")
        list(APPEND _glyphsdf_capture_tests "${_glyphsdf_compare_test}")

        set(_glyphsdf_yaw_test "Application.LookdevCapture.GlyphSdf.AnalyticVsVoxel${_resolution}.Yaw30")
        set(_glyphsdf_yaw_dir "${_glyphsdf_capture_dir}/analytic-vs-voxel-${_resolution}-yaw30")
        add_test(NAME "${_glyphsdf_yaw_test}"
            COMMAND "$<TARGET_FILE:vixen_lookdev_capture>"
                    "--gtest_filter=LookdevCapture.RendersGlyphSdfAnalyticVsVoxel")
        set_tests_properties("${_glyphsdf_yaw_test}" PROPERTIES
            LABELS "Application;GlyphSdfText${_resolution}Yaw30"
            TIMEOUT 1800
            ENVIRONMENT
                "${_lookdev_test_environment};VIXEN_WINDOW_WIDTH=1920;VIXEN_WINDOW_HEIGHT=1080;VIXEN_GLYPHSDF_SCENE=1;VIXEN_GLYPHSDF_COMPARE=1;VIXEN_GLYPHSDF_VARIANT=latin-carved;VIXEN_GLYPHSDF_BAKE_RESOLUTION=${_resolution};VIXEN_LOOKDEV_CAPTURE_ANGLE=0;VIXEN_GLYPHSDF_CAPTURE_YAW_DEGREES=30;VIXEN_LOOKDEV_CAPTURE_FRAMES=1;VIXEN_LOOKDEV_CAPTURE_BUDGET_MS=900000;VIXEN_LOOKDEV_CAPTURE_DIR=${_glyphsdf_yaw_dir};VIXEN_PERF_CSV=${_glyphsdf_yaw_dir}/native-perf.csv"
            ENVIRONMENT_MODIFICATION
                "VIXEN_TEST_CELSHADE_LAMBERT_GGX=set:1;VIXEN_DDGI_CORNELL_BAKED_DEMO=unset:;VIXEN_DDGI_CORNELL_VIRTUAL_DEMO=unset:;VIXEN_DDGI_CORNELL_HYBRID_DEMO=unset:;VIXEN_DDGI_CORNELL_MIXED_DEMO=unset:;VIXEN_POLICY_STENCIL=unset:;VIXEN_POLICY_STENCIL_TILES=unset:;VIXEN_SHADOW_CONFIG_ENABLED=unset:;VIXEN_SHADOW_CONFIG_BIAS_EPSILON=unset:")
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
                    "VIXEN_TEST_CELSHADE_LAMBERT_GGX=set:1;VIXEN_DDGI_CORNELL_BAKED_DEMO=unset:;VIXEN_DDGI_CORNELL_VIRTUAL_DEMO=unset:;VIXEN_DDGI_CORNELL_HYBRID_DEMO=unset:;VIXEN_DDGI_CORNELL_MIXED_DEMO=unset:;VIXEN_GLYPHSDF_SCENE=unset:;VIXEN_GLYPHSDF_COMPARE=unset:;VIXEN_BRICKMAP_SCENE=unset:;VIXEN_POLICY_STENCIL=unset:;VIXEN_POLICY_STENCIL_TILES=unset:;VIXEN_SHADOW_CONFIG_ENABLED=unset:;VIXEN_SHADOW_CONFIG_BIAS_EPSILON=unset:")
            list(APPEND _glyphsdf_capture_tests "${_sdf_filter_nontext_test}")
        endforeach()
    endforeach()

    # Each isolation capture changes exactly one renderer/bake input from the
    # 128^3 non-text sphere baseline. The slow-trilinear variant preserves the
    # eight sampled values but bypasses the contiguous same-brick load path.
    set(_stored_sdf_isolation_tests)
    foreach(_diagnostic IN ITEMS ShadowOff ShadowBiasOneVoxel StoredNormals SlowTrilinear ExtraApron)
        set(_diagnostic_test "Application.LookdevCapture.StoredSdf.Isolation.${_diagnostic}")
        set(_diagnostic_dir "${_glyphsdf_capture_dir}/isolation-${_diagnostic}")
        if(_diagnostic STREQUAL "ShadowOff")
            set(_diagnostic_override "VIXEN_SHADOW_CONFIG_ENABLED=set:0")
        elseif(_diagnostic STREQUAL "ShadowBiasOneVoxel")
            set(_diagnostic_override "VIXEN_SHADOW_CONFIG_BIAS_EPSILON=set:0.15")
        elseif(_diagnostic STREQUAL "StoredNormals")
            set(_diagnostic_override "VIXEN_SHELL_NORMALS=set:1")
        elseif(_diagnostic STREQUAL "SlowTrilinear")
            set(_diagnostic_override "VIXEN_SDF_FORCE_SLOW_TRILINEAR=set:1")
        else()
            set(_diagnostic_override "VIXEN_SDF_FILTER_NON_TEXT_EXTRA_APRON=set:1")
        endif()
        add_test(NAME "${_diagnostic_test}"
            COMMAND "$<TARGET_FILE:vixen_lookdev_capture>"
                    "--gtest_filter=LookdevCapture.RendersSdfFilterNonText")
        set_tests_properties("${_diagnostic_test}" PROPERTIES
            LABELS "Application;StoredSdfIsolation${_diagnostic}"
            TIMEOUT 1800
            ENVIRONMENT
                "${_lookdev_test_environment};VIXEN_WINDOW_WIDTH=1920;VIXEN_WINDOW_HEIGHT=1080;VIXEN_SDF_FILTER_NON_TEXT=1;VIXEN_SDF_FILTER_NON_TEXT_RESOLUTION=128;VIXEN_GLYPHSDF_CAPTURE_YAW_DEGREES=0;VIXEN_LOOKDEV_CAPTURE_STATE=midday;VIXEN_LOOKDEV_CAPTURE_ANGLE=0;VIXEN_LOOKDEV_CAPTURE_FRAMES=1;VIXEN_LOOKDEV_CAPTURE_BUDGET_MS=900000;VIXEN_LOOKDEV_CAPTURE_DIR=${_diagnostic_dir};VIXEN_PERF_CSV=${_diagnostic_dir}/native-perf.csv"
            ENVIRONMENT_MODIFICATION
                "VIXEN_TEST_CELSHADE_LAMBERT_GGX=set:1;VIXEN_DDGI_CORNELL_BAKED_DEMO=unset:;VIXEN_DDGI_CORNELL_VIRTUAL_DEMO=unset:;VIXEN_DDGI_CORNELL_HYBRID_DEMO=unset:;VIXEN_POLICY_STENCIL=unset:;VIXEN_POLICY_STENCIL_TILES=unset:;VIXEN_GLYPHSDF_SCENE=unset:;VIXEN_GLYPHSDF_COMPARE=unset:;VIXEN_BRICKMAP_SCENE=unset:;VIXEN_SDF_FILTER_NON_TEXT_EXTRA_APRON=unset:;VIXEN_SDF_FORCE_SLOW_TRILINEAR=unset:;VIXEN_SHELL_NORMALS=unset:;VIXEN_SHADOW_CONFIG_ENABLED=unset:;VIXEN_SHADOW_CONFIG_BIAS_EPSILON=unset:;${_diagnostic_override}")
        list(APPEND _stored_sdf_isolation_tests "${_diagnostic_test}")
    endforeach()
    vixen_lock_gpu_tests(TESTS ${_glyphsdf_capture_tests} ${_stored_sdf_isolation_tests})
    message(STATUS "[Application Tests] Added: glyph and non-text SDF comparisons at 128^3/256^3 from 0/30 degree yaws")
endif()
