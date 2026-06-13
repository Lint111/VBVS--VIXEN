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
# The copy runs at POST_BUILD so assets track source edits on every build.

function(vixen_stage_assets target src_dir)
    cmake_parse_arguments(VSA "" "DEST" "" ${ARGN})

    if(NOT TARGET ${target})
        message(FATAL_ERROR "vixen_stage_assets: '${target}' is not a target")
    endif()
    if(NOT IS_DIRECTORY "${src_dir}")
        message(WARNING "vixen_stage_assets: source dir does not exist: ${src_dir}")
    endif()

    set(_dest "$<TARGET_FILE_DIR:${target}>")
    if(VSA_DEST)
        set(_dest "${_dest}/${VSA_DEST}")
    endif()

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${src_dir}" "${_dest}"
        COMMENT "vixen_stage_assets: staging ${src_dir} -> ${_dest}"
        VERBATIM)
endfunction()
