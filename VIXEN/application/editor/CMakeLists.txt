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

# Copy TBB DLL to output directory (required by SVO library) — mirrors application/main.
if(TARGET TBB::tbb)
    add_custom_command(TARGET vixen_editor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb>
            $<TARGET_FILE_DIR:vixen_editor>
        COMMENT "Copying TBB DLL for vixen_editor"
    )
endif()

# Stage the shared RmlUi UI assets (fonts + editor.rml/editor.rcss live beside hud.rml/demo.rml
# in libraries/RenderGraph/assets/ui) next to the executable, same helper + same source dir as
# application/main so VIXEN_UI_ASSET_SOURCE_DIR's compiled-in fallback and the staged copy agree.
vixen_stage_assets(vixen_editor ${VIXEN_ROOT}/libraries/RenderGraph/assets DEST assets)

# CTest fixtures produce the live editor and main-app captures before their file-assertion cases.
# Both output directories are absolute build-tree paths so the runners need no source-tree CWD or
# DISPLAY. The shared application test environment supplies Dozen and its loader paths.
if(BUILD_TESTS AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND TARGET VIXEN)
    set(VIXEN_EDITOR_CAPTURE_DIR "${CMAKE_BINARY_DIR}/runtime-captures/editor")
    set(VIXEN_HUD_CAPTURE_DIR "${CMAKE_BINARY_DIR}/runtime-captures/hud")
    file(MAKE_DIRECTORY "${VIXEN_EDITOR_CAPTURE_DIR}" "${VIXEN_HUD_CAPTURE_DIR}")

    add_test(NAME vixen_editor_capture_producer
        COMMAND bash -c
            "exec \"$<TARGET_FILE:vixen_editor>\" > \"${VIXEN_EDITOR_CAPTURE_DIR}/run_editor_script.log\" 2>&1")
    set_tests_properties(vixen_editor_capture_producer PROPERTIES
        FIXTURES_SETUP VixenEditorCapture
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:vixen_editor>"
        TIMEOUT 180)
    vixen_set_suite_properties(RenderGraph TESTS vixen_editor_capture_producer
        TEST_ENVIRONMENT
            "VIXEN_EDITOR_SCRIPT=toggle:2@30,undo@60,redo@90,settings@100,back@110"
            "VIXEN_EDITOR_CAPTURE_FRAMES=5,45,75,105"
            "VIXEN_EDITOR_CAPTURE_DIR=${VIXEN_EDITOR_CAPTURE_DIR}"
            "VIXEN_EDITOR_OFFSCREEN_CAPTURE=1"
            "VIXEN_EDITOR_TEST_CAMERA=top-down"
            "VIXEN_EXIT_AFTER_FRAMES=120")

    add_test(NAME vixen_hud_capture_producer COMMAND $<TARGET_FILE:VIXEN>)
    set_tests_properties(vixen_hud_capture_producer PROPERTIES
        FIXTURES_SETUP VixenHudCapture
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:VIXEN>"
        TIMEOUT 180)
    vixen_set_suite_properties(RenderGraph TESTS vixen_hud_capture_producer
        TEST_ENVIRONMENT
            "VIXEN_HUD_SCRIPT=A@30,B@60"
            "VIXEN_HUD_CAPTURE_FRAMES=5,45,75"
            "VIXEN_HUD_CAPTURE_DIR=${VIXEN_HUD_CAPTURE_DIR}"
            "VIXEN_HUD_OFFSCREEN_CAPTURE=1"
            "VIXEN_TEST_CELSHADE_LAMBERT_GGX=1"
            "VIXEN_EXIT_AFTER_FRAMES=85")
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
            TIMEOUT 300
            LABELS "VIXENCapture;WSL"
            ENVIRONMENT "DISPLAY=:0;${VIXEN_CTEST_GPU_ENVIRONMENT}")
    endif()
endif()
