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
#   vixen_stage_assets(<target> <src-dir> DEST <subdir> [REQUIRE <rel-path>...])
#
#   <target>    A built executable target.
#   <src-dir>   Absolute path to a directory whose CONTENTS are published.
#   DEST        Subdirectory under the exe dir that becomes the runtime path (required: it is
#               replaced by a link, so it cannot be the exe directory itself).
#
# Examples:
#   # libraries/RenderGraph/assets/ui/... -> <exe-dir>/assets/ui/...
#   vixen_stage_assets(MyApp ${CMAKE_SOURCE_DIR}/libraries/RenderGraph/assets DEST assets)
#
#   # data/... -> <exe-dir>/data/...
#   vixen_stage_assets(MyApp ${CMAKE_CURRENT_SOURCE_DIR}/data DEST data)
#
# The assets are published ONCE per source directory as an immutable, digest-keyed bundle
# (<build>/vixen_assets/<srchash>/<key>/ + MANIFEST.txt of per-file sha256), by cmake/VixenAssetBundle.cmake.
# Every consumer destination becomes a symlink <dest> -> <bundle>/current (copy fallback where symlinks
# are unavailable), so N executables/tests never copy the files N times or race each other. Both steps
# are stamp-gated (inputs re-globbed when files are added or removed): a build with no asset edits
# runs no staging command. A missing/corrupt member fails with what/where/why/fix; REQUIRE <rel>...
# lists members that must exist (typed inputs), and ctest vixen_asset_bundle_verify_* re-hashes the bundle.

function(vixen_stage_assets target src_dir)
    cmake_parse_arguments(VSA "" "DEST" "REQUIRE" ${ARGN})

    if(NOT TARGET ${target})
        message(FATAL_ERROR "vixen_stage_assets: '${target}' is not a target")
    endif()
    if(NOT VSA_DEST)
        message(FATAL_ERROR "vixen_stage_assets(${target}): DEST <subdir> is required; the destination becomes a link to the asset bundle and cannot be the executable directory. Fix: add DEST assets (or the subdirectory the runtime loads from).")
    endif()
    if(NOT IS_DIRECTORY "${src_dir}")
        message(WARNING "vixen_stage_assets: source dir does not exist: ${src_dir}")
    endif()

    get_filename_component(src_dir "${src_dir}" REALPATH)   # tests/../assets and <root>/assets are one bundle

    get_target_property(_target_output_dir ${target} RUNTIME_OUTPUT_DIRECTORY)
    if(NOT _target_output_dir AND CMAKE_RUNTIME_OUTPUT_DIRECTORY)
        set(_target_output_dir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    endif()
    if(NOT _target_output_dir)
        # Not $<TARGET_FILE_DIR:target>: inside a custom command it adds a target dependency on the
        # executable, which would make the staging target and the executable depend on each other.
        get_target_property(_target_output_dir ${target} BINARY_DIR)
    endif()

    set(_dest "${_target_output_dir}")
    if(VSA_DEST)
        set(_dest "${_dest}/${VSA_DEST}")
    endif()

    set(_script "${VIXEN_ROOT}/cmake/VixenAssetBundle.cmake")
    set(_assets_root "${CMAKE_BINARY_DIR}/vixen_assets")

    # One bundle per source directory (digest-keyed, immutable, published once per build tree).
    string(SHA256 _src_hash "${src_dir}")
    string(SUBSTRING "${_src_hash}" 0 12 _src_hash)
    set(_bundle_root "${_assets_root}/${_src_hash}")
    set(_bundle_target "vixen_asset_bundle_${_src_hash}")
    if(NOT TARGET ${_bundle_target})
        file(GLOB_RECURSE _bundle_inputs CONFIGURE_DEPENDS LIST_DIRECTORIES false "${src_dir}/*")
        set(_bundle_stamp "${_bundle_root}/bundle.stamp")
        add_custom_command(OUTPUT "${_bundle_stamp}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_bundle_root}"
            COMMAND ${CMAKE_COMMAND} -DMODE=bundle "-DSRC=${src_dir}" "-DROOT=${_bundle_root}"
                    "-DSTAMP=${_bundle_stamp}" "-DREQUIRE=${VSA_REQUIRE}" -P "${_script}"
            DEPENDS ${_bundle_inputs} "${_script}"
            COMMENT "vixen_stage_assets: publishing asset bundle ${src_dir}"
            VERBATIM)
        add_custom_target(${_bundle_target} DEPENDS "${_bundle_stamp}")
        set_property(GLOBAL APPEND PROPERTY VIXEN_ASSET_BUNDLE_ROOTS "${_bundle_root}")
        if(BUILD_TESTS)
            add_test(NAME vixen_asset_bundle_verify_${_src_hash}
                COMMAND ${CMAKE_COMMAND} -DMODE=verify "-DROOT=${_bundle_root}" -P "${_script}")
        endif()
    elseif(VSA_REQUIRE)
        message(WARNING "vixen_stage_assets: REQUIRE ignored; bundle for ${src_dir} was declared earlier")
    endif()

    # Per-destination stable runtime path: <dest> -> <bundle>/current (symlink; copy fallback).
    string(SHA256 _dest_hash "${_src_hash}|${_dest}")
    string(SUBSTRING "${_dest_hash}" 0 16 _dest_hash)
    set(_link_target "vixen_stage_assets_${_dest_hash}")
    if(NOT TARGET ${_link_target})
        set(_link_stamp "${_assets_root}/links/${_dest_hash}.stamp")
        add_custom_command(OUTPUT "${_link_stamp}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_assets_root}/links"
            COMMAND ${CMAKE_COMMAND} -DMODE=link "-DROOT=${_bundle_root}" "-DDEST=${_dest}"
                    "-DSTAMP=${_link_stamp}" -P "${_script}"
            DEPENDS "${_script}"
            COMMENT "vixen_stage_assets: linking ${_dest} -> asset bundle"
            VERBATIM)
        add_custom_target(${_link_target} DEPENDS "${_link_stamp}")
        add_dependencies(${_link_target} ${_bundle_target})
    endif()

    add_dependencies(${target} ${_link_target})
endfunction()
