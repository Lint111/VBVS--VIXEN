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
target_link_libraries(vixen_editor PRIVATE AppFlow)

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
