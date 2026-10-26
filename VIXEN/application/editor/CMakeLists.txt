# ============================================================================
# vixen_editor — Inc1 VoxelDocument authoring app (load -> render -> toggle -> save)
# ============================================================================
# Links the shared VixenApp driver library (built by application/main/CMakeLists.txt) so it
# reuses the whole engine graph/lifecycle unmodified; only EditorApplication (a
# VulkanGraphApplication subclass) and its own UI document are new.

cmake_minimum_required(VERSION 3.15...4.2 FATAL_ERROR)

add_executable(vixen_editor
    ${CMAKE_CURRENT_SOURCE_DIR}/source/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/EditorApplication.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/AppFlowChannel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/AppFlowReadback.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/EditorLayersViewBridge.cpp
)

target_include_directories(vixen_editor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Inc-1 M4: must whole-archive VixenApp, not link it plainly, or the scoped node OBJECT libs
# merged into VixenApp.lib get dropped at this link step -- see cmake/VixenNodeLinkage.cmake.
# Side effect of whole-archiving: vixen_editor's own StbImageWriteImpl.cpp (a second
# STB_IMAGE_WRITE_IMPLEMENTATION TU) used to coexist with VixenApp's copy only because plain
# (non-whole-archive) linking meant archive-member-selection never actually pulled VixenApp's
# copy in (vixen_editor's own copy already satisfied every reference). Whole-archiving forces
# BOTH copies in -> LNK2005 duplicate symbols. Fixed by deleting vixen_editor's redundant copy
# above (VixenApp already provides EXACTLY one implementation of stb_image_write.h, and
# whole-archiving now guarantees it's actually linked in, unconditionally).
vixen_whole_archive_link_vixen_app(vixen_editor PRIVATE)
# Inc-B (View-Model-Binding-Inc-B-Plan-2026-07.md): the editor now owns a live GaiaVoxelWorld
# (EditorApplication::gaiaWorld_) for the Gaia-backed layer-mask provider + reconcile -- previously
# vixen_editor only pulled gaia.h TRANSITIVELY via SVO's octree headers (VixenApp), never linked
# the library that actually instantiates a World.
target_link_libraries(vixen_editor PRIVATE AppFlow GaiaVoxelWorld::GaiaVoxelWorld VoxelDocument)
if(WIN32)
    target_link_libraries(vixen_editor PRIVATE ws2_32)
endif()

# Default document path (no argv[1]) resolves relative to the source-tree BuiltAssets dir, not
# the exe's CWD -- so `vixen_editor` works from any working directory.
target_compile_definitions(vixen_editor PRIVATE
    VIXEN_EDITOR_DEFAULT_DOCUMENT="${VIXEN_ROOT}/BuiltAssets/documents/sample_tri_layer.vxd"
    VIXEN_EDITOR_APPFLOW_PATH="${VIXEN_ROOT}/libraries/AppFlow/assets/AppFlow.appflow")

set_property(TARGET vixen_editor PROPERTY RUNTIME_OUTPUT_DIRECTORY ${VIXEN_BINARIES_DIR})
set_property(TARGET vixen_editor PROPERTY RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VIXEN_BINARIES_DIR})
set_property(TARGET vixen_editor PROPERTY RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VIXEN_BINARIES_DIR})
set_property(TARGET vixen_editor PROPERTY RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${VIXEN_BINARIES_DIR})
set_property(TARGET vixen_editor PROPERTY RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${VIXEN_BINARIES_DIR})
set_property(TARGET vixen_editor PROPERTY CXX_STANDARD 23)
set_property(TARGET vixen_editor PROPERTY CXX_STANDARD_REQUIRED ON)
set_target_properties(vixen_editor PROPERTIES FOLDER "Application")

if(MSVC)
    target_compile_options(vixen_editor PRIVATE /FS)
endif()

# Stage the shared RmlUi UI assets (fonts + the editor panel documents, editor.rcss and dock.rml live beside hud.rml/demo.rml
# in libraries/RenderGraph/assets/ui) next to the executable, same helper + same source dir as
# application/main so VIXEN_UI_ASSET_SOURCE_DIR's compiled-in fallback and the staged copy agree.
vixen_stage_assets(vixen_editor ${VIXEN_ROOT}/libraries/RenderGraph/assets DEST assets)

# CTest fixtures produce the live editor and main-app captures before their file-assertion cases.
# The build-tree directories are the non-queue fallback; Linux command wrappers switch each GPU
# process to UT_QUEUE_CAPTURE_ROOT at run time so documents, logs, and captures stay together.
# The shared application test environment supplies Dozen and its loader paths.
if(BUILD_TESTS AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND TARGET VIXEN)
    set(VIXEN_EDITOR_CAPTURE_ROOT "${CMAKE_BINARY_DIR}/runtime-captures")
    set(VIXEN_EDITOR_CAPTURE_DIR "${VIXEN_EDITOR_CAPTURE_ROOT}/editor")
    file(MAKE_DIRECTORY "${VIXEN_EDITOR_CAPTURE_DIR}")
    # SaveDocument writes beside its input. Give the capture producer a build-tree copy so its
    # save/reopen script never mutates the tracked BuiltAssets sample.
    set(VIXEN_EDITOR_TEST_DOCUMENT "${VIXEN_EDITOR_CAPTURE_DIR}/sample_tri_layer.vxd")
    configure_file("${VIXEN_ROOT}/BuiltAssets/documents/sample_tri_layer.vxd"
        "${VIXEN_EDITOR_TEST_DOCUMENT}" COPYONLY)

    add_test(NAME vixen_editor_capture_producer
        COMMAND bash -c
            "set -eu
            capture_root=\"\${UT_QUEUE_CAPTURE_ROOT:-${VIXEN_EDITOR_CAPTURE_ROOT}}\"
            capture_dir=\"\${capture_root}/editor\"
            document=\"\${capture_dir}/sample_tri_layer.vxd\"
            mkdir -p \"\${capture_dir}\"
            cp -- \"${VIXEN_ROOT}/BuiltAssets/documents/sample_tri_layer.vxd\" \"\${document}\"
            export VIXEN_EDITOR_CAPTURE_DIR=\"\${capture_dir}\"
            exec \"$<TARGET_FILE:vixen_editor>\" \"\${document}\" > \"\${capture_dir}/run_editor_script.log\" 2>&1")
    set_tests_properties(vixen_editor_capture_producer PROPERTIES
        FIXTURES_SETUP VixenEditorCapture
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:vixen_editor>"
        TIMEOUT 360)
    vixen_set_suite_properties(RenderGraph GPU TESTS vixen_editor_capture_producer
        TEST_ENVIRONMENT
            # R426 makes bi-modal cel the body-shading default; keep the legacy test override off.
            "VIXEN_TEST_CELSHADE_LAMBERT_GGX=0"
            "VIXEN_EDITOR_SCRIPT=parameter_up:0@20,parameter_up:0@21,parameter_up:0@22,parameter_up:0@23,toggle:2@30,undo@60,redo@90,save@100,reopen@110,settings@120,back@130"
            "VIXEN_EDITOR_CAPTURE_FRAMES=5,25,45,75,105,115"
            "VIXEN_EDITOR_CAPTURE_DIR=${VIXEN_EDITOR_CAPTURE_DIR}"
            "VIXEN_EDITOR_OFFSCREEN_CAPTURE=1"
            "VIXEN_EDITOR_TEST_CAMERA=top-down"
            # Undo/redo must restore the frame byte for byte, so eye adaptation is made instant
            # here (the maximum 0.5 s step x 100/s leaves e^-50 of the gap): exposure is then a
            # function of the frame, not of its history.
            "VIXEN_HDR_EXPOSURE_DT=0.5"
            "VIXEN_HDR_EXPOSURE_SPEED_UP=100"
            "VIXEN_HDR_EXPOSURE_SPEED_DOWN=100"
            "VIXEN_EXIT_AFTER_FRAMES=140")

    if(VIXEN_FAIL_SCENARIOS)
        set(VIXEN_DEVICE_LOSS_LOG "${CMAKE_BINARY_DIR}/runtime-captures/device_loss_recovery.log")
        add_test(NAME vixen_device_loss_recovery
            COMMAND bash -c
                "mkdir -p \"$(dirname \"${VIXEN_DEVICE_LOSS_LOG}\")\" && \"$<TARGET_FILE:VIXEN>\" > \"${VIXEN_DEVICE_LOSS_LOG}\" 2>&1 && grep -q 'RECOVERY COMPLETE' \"${VIXEN_DEVICE_LOSS_LOG}\"")
        set_tests_properties(vixen_device_loss_recovery PROPERTIES
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:VIXEN>"
            TIMEOUT 180)
        vixen_set_suite_properties(RenderGraph GPU TESTS vixen_device_loss_recovery
            TEST_ENVIRONMENT
                "VIXEN_SIMULATE_DEVICE_LOSS=40"
                "VIXEN_HUD_OFFSCREEN_CAPTURE=1"
                "VIXEN_TEST_CELSHADE_LAMBERT_GGX=1"
                "VIXEN_EXIT_AFTER_FRAMES=85")
    endif()
endif()

# Keep the HUD fixture available to the native Windows capture lane as well as Dozen. Look-dev
# scene captures intentionally suppress the HUD, so text parity has its own independently queued
# producer and label on every platform.
if(BUILD_TESTS AND TARGET VIXEN)
    set(VIXEN_HUD_CAPTURE_DIR "${CMAKE_BINARY_DIR}/runtime-captures/hud")
    file(MAKE_DIRECTORY "${VIXEN_HUD_CAPTURE_DIR}")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_test(NAME vixen_hud_capture_producer
            COMMAND bash -c
                "set -eu
                capture_root=\"\${UT_QUEUE_CAPTURE_ROOT:-${CMAKE_BINARY_DIR}/runtime-captures}\"
                capture_dir=\"\${capture_root}/hud\"
                mkdir -p \"\${capture_dir}\"
                export VIXEN_HUD_CAPTURE_DIR=\"\${capture_dir}\"
                exec \"$<TARGET_FILE:VIXEN>\"")
    else()
        add_test(NAME vixen_hud_capture_producer COMMAND $<TARGET_FILE:VIXEN>)
    endif()
    set_tests_properties(vixen_hud_capture_producer PROPERTIES
        FIXTURES_SETUP VixenHudCapture
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:VIXEN>"
        TIMEOUT 180)
    vixen_set_suite_properties(RenderGraph GPU TESTS vixen_hud_capture_producer
        TEST_ENVIRONMENT
            "VIXEN_HUD_SCRIPT=A@30,B@60"
            "VIXEN_HUD_CAPTURE_FRAMES=5,45,75"
            "VIXEN_HUD_CAPTURE_DIR=${VIXEN_HUD_CAPTURE_DIR}"
            "VIXEN_HUD_OFFSCREEN_CAPTURE=1"
            "VIXEN_TEST_CELSHADE_LAMBERT_GGX=1"
            "VIXEN_EXIT_AFTER_FRAMES=85")
    set_property(TEST vixen_hud_capture_producer APPEND PROPERTY LABELS VIXEN_HUD_CAPTURE)

    # Separate opt-in producer for the generated MSDF text shader. Its own directory and label keep
    # the baseline HUD producer byte-stable while native and Dozen runs capture animation at frames
    # 5 and 45.
    set(VIXEN_TEXT_SHADER_CAPTURE_DIR "${CMAKE_BINARY_DIR}/runtime-captures/textshader")
    file(MAKE_DIRECTORY "${VIXEN_TEXT_SHADER_CAPTURE_DIR}")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_test(NAME vixen_text_shader_capture_producer
            COMMAND bash -c
                "set -eu
                capture_root=\"\${UT_QUEUE_CAPTURE_ROOT:-${CMAKE_BINARY_DIR}/runtime-captures}\"
                capture_dir=\"\${capture_root}/textshader\"
                mkdir -p \"\${capture_dir}\"
                export VIXEN_HUD_CAPTURE_DIR=\"\${capture_dir}\"
                exec \"$<TARGET_FILE:VIXEN>\"")
    else()
        add_test(NAME vixen_text_shader_capture_producer COMMAND $<TARGET_FILE:VIXEN>)
    endif()
    set_tests_properties(vixen_text_shader_capture_producer PROPERTIES
        FIXTURES_SETUP VixenTextShaderCapture
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:VIXEN>"
        TIMEOUT 180)
    vixen_set_suite_properties(RenderGraph GPU TESTS vixen_text_shader_capture_producer
        TEST_ENVIRONMENT
            "VIXEN_TEXT_SHADER_CAPTURE=1"
            "VIXEN_HUD_CAPTURE_FRAMES=5,45"
            "VIXEN_HUD_CAPTURE_DIR=${VIXEN_TEXT_SHADER_CAPTURE_DIR}"
            "VIXEN_HUD_OFFSCREEN_CAPTURE=1"
            "VIXEN_WINDOW_WIDTH=1920"
            "VIXEN_WINDOW_HEIGHT=1080"
            "VIXEN_TEST_CELSHADE_LAMBERT_GGX=1"
            "VIXEN_EXIT_AFTER_FRAMES=55")
    set_property(TEST vixen_text_shader_capture_producer APPEND PROPERTY LABELS VIXEN_TEXT_SHADER_CAPTURE)
endif()

# The native windowed witness is WSLg-specific. Keep its display contract in
# CTest so callers can run the test with DISPLAY unset, while ordinary capture
# fixtures above remain offscreen and display-independent.
if(BUILD_TESTS AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND TARGET VIXEN AND TARGET vixen_editor
        AND EXISTS "/proc/sys/kernel/osrelease")
    file(READ "/proc/sys/kernel/osrelease" _vixen_kernel_release)
    if(_vixen_kernel_release MATCHES "[Mm]icrosoft" AND EXISTS "/tmp/.X11-unix/X0")
        set(VIXEN_NATIVE_CAPTURE_DIR "${CMAKE_BINARY_DIR}/runtime-captures/native")
        add_test(NAME vixen_wsl_capture_witness
            COMMAND bash "${VIXEN_ROOT}/../tools/run-vixen-windowed-captures.sh"
                "$<TARGET_FILE_DIR:VIXEN>" "$<TARGET_FILE_NAME:VIXEN>"
                "$<TARGET_FILE_DIR:vixen_editor>" "$<TARGET_FILE_NAME:vixen_editor>"
                "${VIXEN_NATIVE_CAPTURE_DIR}")
        set_tests_properties(vixen_wsl_capture_witness PROPERTIES
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:VIXEN>"
            TIMEOUT 600
            LABELS "VIXENCapture;WSL"
            ENVIRONMENT "DISPLAY=:0;${VIXEN_CTEST_GPU_ENVIRONMENT}")
        vixen_lock_gpu_tests(TESTS vixen_wsl_capture_witness)

        # The instancing demo renders straight to a window swapchain (no offscreen path), so its
        # producer shares this block's WSLg requirement. Capture root follows the same
        # UT_QUEUE_CAPTURE_ROOT contract as the HUD and editor producers.
        add_test(NAME vixen_instancing_demo_capture_producer
            COMMAND bash -c
                "set -eu
                capture_root=\"\${UT_QUEUE_CAPTURE_ROOT:-${CMAKE_BINARY_DIR}/runtime-captures}\"
                capture_dir=\"\${capture_root}/instancing\"
                mkdir -p \"\${capture_dir}\"
                rm -f \"\${capture_dir}\"/hud_capture_*.png
                export VIXEN_HUD_CAPTURE_DIR=\"\${capture_dir}\"
                \"$<TARGET_FILE:VIXEN>\" > \"\${capture_dir}/instancing_demo.log\" 2>&1
                for frame in 5 45 75; do test -s \"\${capture_dir}/hud_capture_\${frame}.png\"; done")
        set_tests_properties(vixen_instancing_demo_capture_producer PROPERTIES
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:VIXEN>"
            TIMEOUT 180)
        vixen_set_suite_properties(RenderGraph GPU TESTS vixen_instancing_demo_capture_producer
            TEST_ENVIRONMENT
                "DISPLAY=:0"
                "VIXEN_INSTANCING_DEMO=1"
                "VIXEN_HUD_CAPTURE_FRAMES=5,45,75"
                "VIXEN_EXIT_AFTER_FRAMES=85")
        set_property(TEST vixen_instancing_demo_capture_producer APPEND PROPERTY LABELS VIXEN_INSTANCING_CAPTURE)
    endif()
endif()

if(BUILD_TESTS AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND TARGET VIXEN AND TARGET vixen_editor)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME vixen_editor_appflow_channel_gate
        COMMAND bash -c
            "set -eu
            capture_root=\"\${UT_QUEUE_CAPTURE_ROOT:-${VIXEN_EDITOR_CAPTURE_ROOT}}\"
            capture_dir=\"\${capture_root}/editor\"
            document=\"\${capture_dir}/sample_tri_layer.vxd\"
            transcript=\"\${capture_dir}/appflow-channel-transcript.json\"
            mkdir -p \"\${capture_dir}\"
            cp -- \"${VIXEN_ROOT}/BuiltAssets/documents/sample_tri_layer.vxd\" \"\${document}\"
            export VIXEN_EDITOR_CAPTURE_DIR=\"\${capture_dir}\"
            exec \"${Python3_EXECUTABLE}\" \"${VIXEN_ROOT}/tools/appflow_channel_gate.py\" --editor \"$<TARGET_FILE:vixen_editor>\" --server \"${VIXEN_ROOT}/tools/appflow_mcp_server.py\" --document \"\${document}\" --transcript \"\${transcript}\"")
    set_tests_properties(vixen_editor_appflow_channel_gate PROPERTIES
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:vixen_editor>"
        TIMEOUT 300)
    vixen_set_suite_properties(RenderGraph GPU TESTS vixen_editor_appflow_channel_gate
        TEST_ENVIRONMENT
            "VIXEN_TEST_CELSHADE_LAMBERT_GGX=0"
            "VIXEN_EDITOR_OFFSCREEN_CAPTURE=1")
    vixen_lock_gpu_tests(TESTS vixen_editor_appflow_channel_gate)
endif()

# Multi-process cache correctness gate. The test owns its process fan-out so CTest admits it as
# one GPU job; each child gets unique capture/document paths while all children share VIXEN_CACHE_DIR.
# It is registered under the `nightly` label only (R565 C5): it tests cache collisions, not a lane's
# change, and its cold pipeline compiles held 45-60% of every lane's GPU-queue time. The split runner
# excludes `nightly` by default; `ctest -L nightly` or `run-tests-split.sh --nightly` runs it.
if(BUILD_TESTS AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND TARGET VIXEN AND TARGET vixen_editor)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME vixen_multiinstance_cache_gate
        COMMAND ${Python3_EXECUTABLE} "${VIXEN_ROOT}/tools/multiinstance_cache_gate.py"
            --mode gate
            --app "$<TARGET_FILE:VIXEN>"
            --editor "$<TARGET_FILE:vixen_editor>"
            --document "${VIXEN_ROOT}/BuiltAssets/documents/sample_tri_layer.vxd"
            --work-dir "${CMAKE_BINARY_DIR}/runtime-captures/multiinstance")
    set_tests_properties(vixen_multiinstance_cache_gate PROPERTIES TIMEOUT 1200)
    vixen_set_suite_properties(nightly GPU TESTS vixen_multiinstance_cache_gate)
endif()
vixen_schedule_runtime_dlls_for_current_directory()
