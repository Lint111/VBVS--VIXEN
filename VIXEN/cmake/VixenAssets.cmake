# VixenAssets.cmake
#
# Reusable runtime-asset staging for VIXEN apps and consumers.
#
# Engine and consumer code loads runtime assets (fonts, RML/RCSS, shaders, data
# files) via paths relative to the executable. Without staging, those files are
# not next to the built exe and loading fails. Before this helper every consumer
# hand-wrote an add_custom_command(... POST_BUILD ... copy_directory ...) by hand
# (UNDERTOW FR-10); this provides one documented call instead.
#
# Usage:
#   vixen_stage_assets(<target> <src-dir> [DEST <subdir>])
#
#   <target>    A built executable target.
#   <src-dir>   Absolute path to a directory whose CONTENTS are copied.
#   DEST        Optional subdirectory under the exe dir to copy into. Omit to
#               copy directly next to the executable.
#
# Examples:
#   # libraries/RenderGraph/assets/ui/... -> <exe-dir>/assets/ui/...
#   vixen_stage_assets(MyApp ${CMAKE_SOURCE_DIR}/libraries/RenderGraph/assets DEST assets)
#
#   # data/... -> <exe-dir>/...
#   vixen_stage_assets(MyApp ${CMAKE_CURRENT_SOURCE_DIR}/data)
#
# The staging target is a shared build dependency, so several executables using the same assets
# and output directory schedule one copy job instead of racing separate POST_BUILD commands.
# The copy is stamp-gated on the staged files (re-globbed when files are added or removed), so a
# build with no asset edits runs no staging command.

function(vixen_stage_assets target src_dir)
    cmake_parse_arguments(VSA "" "DEST" "" ${ARGN})

    if(NOT TARGET ${target})
        message(FATAL_ERROR "vixen_stage_assets: '${target}' is not a target")
    endif()
    if(NOT IS_DIRECTORY "${src_dir}")
        message(WARNING "vixen_stage_assets: source dir does not exist: ${src_dir}")
    endif()

    get_target_property(_target_output_dir ${target} RUNTIME_OUTPUT_DIRECTORY)
    if(NOT _target_output_dir)
        set(_target_output_dir "$<TARGET_FILE_DIR:${target}>")
    endif()

    set(_dest "${_target_output_dir}")
    if(VSA_DEST)
        set(_dest "${_dest}/${VSA_DEST}")
    endif()

    string(SHA256 _stage_hash "${src_dir}|${_dest}")
    string(SUBSTRING "${_stage_hash}" 0 16 _stage_hash)
    set(_stage_target "vixen_stage_assets_${_stage_hash}")

    if(NOT TARGET ${_stage_target})
        file(GLOB_RECURSE _stage_inputs CONFIGURE_DEPENDS LIST_DIRECTORIES false "${src_dir}/*")
        set(_stage_stamp "${CMAKE_CURRENT_BINARY_DIR}/vixen_stage_assets/${_stage_hash}.stamp")
        add_custom_command(OUTPUT "${_stage_stamp}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_dest}"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${src_dir}" "${_dest}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/vixen_stage_assets"
            COMMAND ${CMAKE_COMMAND} -E touch "${_stage_stamp}"
            DEPENDS ${_stage_inputs}
            COMMENT "vixen_stage_assets: staging ${src_dir} -> ${_dest}"
            VERBATIM)
        add_custom_target(${_stage_target} DEPENDS "${_stage_stamp}")
    endif()

    add_dependencies(${target} ${_stage_target})
endfunction()
