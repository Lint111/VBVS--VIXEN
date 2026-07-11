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
    ${CMAKE_CURRENT_SOURCE_DIR}/source/StbImageWriteImpl.cpp
)

target_include_directories(vixen_editor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(vixen_editor PRIVATE VixenApp AppFlow)

# Inc-2b Task 3: stb_image_write.h (Debug/RenderTargetReadback.h's PNG writer, called from
# EditorApplication::CaptureFrameToPng) -- not transitively provided by VixenApp/RenderGraph.
if(TARGET stb)
    target_link_libraries(vixen_editor PRIVATE stb)
endif()

# Default document path (no argv[1]) resolves relative to the source-tree BuiltAssets dir, not
# the exe's CWD -- so `vixen_editor` works from any working directory.
target_compile_definitions(vixen_editor PRIVATE
    VIXEN_EDITOR_DEFAULT_DOCUMENT="${VIXEN_ROOT}/BuiltAssets/documents/sample_tri_layer.vxd")

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
