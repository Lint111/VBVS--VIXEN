# VixenNodeLinkage.cmake
#
# Scoped node linkage (Graph-Derived-Node-Linkage-Spec-2026-07 §2.3, Inc-1 M4): links a target
# against only the RenderGraphNode_<Name> OBJECT libraries its own generated node manifest
# (cmake/VixenNodeManifest.cmake) says it actually uses, instead of the unscoped RenderGraph
# facade's whole-archived RenderGraphNodes (which pulls in every node type).
#
# Type-name -> OBJECT-lib-target mapping (verified for all 45 Inc-1 M3 manifest entries against
# libraries/RenderGraph/CMakeLists.txt's per-node OBJECT-lib loop):
#   <Basename>NodeType  (manifest entry, e.g. CameraNodeType)
#     -> source file src/Nodes/<Basename>Node.cpp
#     -> OBJECT lib target RenderGraphNode_<Basename>Node  (e.g. RenderGraphNode_CameraNode)
# i.e. strip the manifest entry's trailing "Type", the result IS the OBJECT-lib target's name
# suffix. One node source (ConstantNode.cpp) registers two NodeTypes from a single TU/OBJECT
# lib (ConstantNodeType + ShaderConstantNodeType) -- both map to the same RenderGraphNode_
# ConstantNode target, so mapping a manifest that only contains ConstantNodeType still links the
# TU that also contains ShaderConstantNodeType (harmless, same OBJECT lib either way).
#
# UIRenderNodeType is not a special case: UIRenderNode.cpp was originally compiled directly into
# the RenderGraphNodes static lib (bundled with genuinely non-node UI support code --
# VixenRmlRenderInterface, ViewStore, BlobView, ...), which had no per-node OBJECT lib to map to
# and would have forced a fallback to whole-archiving all of RenderGraphNodes (defeating scoping
# entirely, since that facade PUBLIC-links every node's OBJECT lib). Fixed at the source: Inc-1 M4
# moved UIRenderNode.cpp out of RENDERGRAPH_UI_SOURCES and into RENDERGRAPH_NODE_SOURCES in
# libraries/RenderGraph/CMakeLists.txt, so it gets a real RenderGraphNode_UIRenderNode OBJECT lib
# like every other node and the mechanical mapping rule above applies uniformly, no exception.
#
# Usage:
#   vixen_link_used_nodes(<target> MANIFEST_FILE <path-to-node_manifest.cmake>)
#
# Includes the manifest file (defines VIXEN_APP_USED_NODE_TYPES), maps each entry to its
# RenderGraphNode_<Name> OBJECT-lib target, and PUBLIC-links every mapped target onto <target>.
# PUBLIC (not PRIVATE) matters here: <target> is typically a library (e.g. VixenApp) with its
# own downstream consumers (the VIXEN exe, vixen_editor, offline unit tests) that compile TUs
# directly including node/RmlUi headers (e.g. EditorLayersViewBridge.cpp, test_hud_view.cpp) --
# those consumers need the node libs' transitive usage requirements (include dirs, RMLUI_STATIC_LIB)
# re-exported through <target>, exactly as the old unscoped RenderGraph facade did. PRIVATE here
# silently breaks every such downstream consumer with "Cannot open include file: RmlUi/Core.h"
# (caught empirically during Inc-1 M4 verification, not assumed).
#
# Hard-errors at configure time if a mapped target name doesn't exist as a CMake target -- this
# is the structural verification called for by spec §4: manifest generation and linkage read the
# same source-derived data, so a missing target here means the mapping rule itself broke, not a
# separate check to maintain.
#
# WHOLE-ARCHIVE IS REQUIRED, even for OBJECT libraries (learned empirically during Inc-1 M4, this
# contradicts the spec §2.3 assumption that "OBJECT library members are unconditionally included,
# no stripping to defeat"). Each node .cpp self-registers via VIXEN_REGISTER_NODE, a file-scope
# static initializer with no other symbol referenced anywhere else in the program (see
# libraries/RenderGraph/include/Core/NodeRegistration.h's own comment: "RenderGraphNodes MUST be
# linked whole-archive or the linker strips these registrars"). That claim is true, but the spec
# missed that it ALSO applies one layer deeper than it assumed: <target> here (VixenApp) is a
# STATIC library, not a final executable -- and when a STATIC archive (VixenApp.lib) is itself
# linked into the real executable (VIXEN.exe), the linker's *archive member selection* only pulls
# in .obj members that resolve some unresolved external. A node OBJECT lib's .obj file, once
# absorbed into VixenApp.lib via a plain (non-whole-archive) link, is just another archive member
# subject to that same selection -- nothing in main.cpp ever references a symbol from
# InstanceNode.cpp, so the linker drops it, and RegisterAllNodes() never runs its registrar at
# startup: "Node type not registered: class Vixen::RenderGraph::InstanceNodeType" at graph-build
# time (reproduced, not hypothesized -- this is exactly the spec §3 loud-failure scenario, and
# exactly why it must be fixed here rather than routed around). Fix: whole-archive every mapped
# node OBJECT lib the same way RenderGraph's facade already whole-archives RenderGraphNodes.

# EXTRA FIX (still Inc-1 M4, found immediately after the fix above): whole-archiving the node
# OBJECT libs onto VixenApp (as done below) is not sufficient by itself. VixenApp is ALSO a
# STATIC library, and CMake's $<LINK_LIBRARY:WHOLE_ARCHIVE,X>` generator expression, when set as
# a PUBLIC usage requirement on a STATIC library target, does NOT propagate through that STATIC
# library to ITS OWN consumers -- verified empirically by inspecting the real generated VIXEN.exe
# link.exe command line (via `cmake --build . --target VIXEN -v`), which showed plain
# `lib\VixenApp.lib` with zero `/WHOLEARCHIVE:` flags anywhere, even after whole-archiving every
# node target onto VixenApp. This is a genuine limitation of nesting whole-archive requirements
# through an intermediate STATIC archive, not a mistake in the node-target whole-archiving itself
# (reproduced: registered node count dropped to 8/54 with this bug, vs. the 45/54 unscoped-facade-
# equivalent count once fixed). The actual fix has to happen at VixenApp's OWN consumers (VIXEN,
# vixen_editor, the offline unit tests, test_fail_scenario_sweep): they must whole-archive
# VixenApp.lib itself, not just link it plainly -- see vixen_whole_archive_link_vixen_app() below.
# Whole-archiving VixenApp.lib in full is not a scope regression versus VixenApp's own non-node
# code (VulkanApplicationBase.cpp etc.) -- that code was always needed by every VixenApp consumer
# unconditionally, it's only the NODE OBJECT libs merged into VixenApp.lib that scoping controls,
# and whole-archiving the outer archive is what makes those merged-in node objects survive to the
# final link, exactly the same archive-member-selection problem one layer up.

# Whole-archives one target onto <target_name>, PUBLIC, using whichever mechanism this CMake/
# linker combination supports -- mirrors libraries/RenderGraph/CMakeLists.txt's own three-way
# dialect switch for whole-archiving RenderGraphNodes onto the RenderGraph facade.
function(_vixen_whole_archive_link target_name lib_target)
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
        target_link_libraries(${target_name} PUBLIC
            "$<LINK_LIBRARY:WHOLE_ARCHIVE,${lib_target}>")
    elseif(MSVC)
        target_link_libraries(${target_name} PUBLIC ${lib_target})
        target_link_options(${target_name} PUBLIC
            "/WHOLEARCHIVE:$<TARGET_FILE:${lib_target}>")
    else()
        target_link_libraries(${target_name} PUBLIC
            "-Wl,--whole-archive" ${lib_target} "-Wl,--no-whole-archive")
    endif()
endfunction()

function(vixen_link_used_nodes target_name)
    cmake_parse_arguments(VLUN "" "MANIFEST_FILE" "" ${ARGN})

    if(NOT VLUN_MANIFEST_FILE)
        message(FATAL_ERROR "vixen_link_used_nodes: MANIFEST_FILE is required")
    endif()
    if(NOT EXISTS "${VLUN_MANIFEST_FILE}")
        message(FATAL_ERROR "vixen_link_used_nodes: manifest file does not exist: ${VLUN_MANIFEST_FILE}")
    endif()

    include("${VLUN_MANIFEST_FILE}")

    if(NOT DEFINED VIXEN_APP_USED_NODE_TYPES)
        message(FATAL_ERROR "vixen_link_used_nodes: ${VLUN_MANIFEST_FILE} did not define VIXEN_APP_USED_NODE_TYPES")
    endif()

    set(_linked_count 0)
    foreach(_node_type IN LISTS VIXEN_APP_USED_NODE_TYPES)
        string(REGEX REPLACE "Type$" "" _node_basename "${_node_type}")
        set(_node_target "RenderGraphNode_${_node_basename}")

        if(NOT TARGET ${_node_target})
            message(FATAL_ERROR
                "vixen_link_used_nodes: manifest entry '${_node_type}' (from "
                "${VLUN_MANIFEST_FILE}) mapped to '${_node_target}', but no such CMake target "
                "exists. Either the node's OBJECT-lib target name changed in "
                "libraries/RenderGraph/CMakeLists.txt, or this is not a real node type. Fix the "
                "mapping rule in cmake/VixenNodeLinkage.cmake -- do not hand-patch the manifest.")
        endif()

        _vixen_whole_archive_link(${target_name} ${_node_target})
        math(EXPR _linked_count "${_linked_count} + 1")

        # RenderGraphNode_UIRenderNode PUBLIC-depends on RenderGraphUiSupport (the non-node RmlUi
        # backend code -- VixenRmlRenderInterface, ViewStore, BlobView, ... -- UIRenderNode calls
        # into). That dependency is itself an OBJECT lib and needs the same whole-archive treatment
        # for the same archive-member-selection reason documented above -- a plain link lets
        # VixenApp.lib's/VIXEN.exe's link step drop its .obj members since nothing outside
        # UIRenderNode.cpp references their symbols directly (reproduced as LNK2019 unresolved
        # VixenRmlRenderInterface::* symbols before this fix). Whole-archiving the node target
        # alone does not transitively whole-archive its own OBJECT-lib dependencies.
        if(_node_target STREQUAL "RenderGraphNode_UIRenderNode" AND TARGET RenderGraphUiSupport)
            _vixen_whole_archive_link(${target_name} RenderGraphUiSupport)
        endif()
    endforeach()

    message(STATUS "vixen_link_used_nodes: ${target_name} scoped-linked ${_linked_count} node OBJECT librar(y/ies) (whole-archived) from ${VLUN_MANIFEST_FILE}")
endfunction()

# vixen_whole_archive_link_vixen_app(<target> [PRIVATE|PUBLIC|INTERFACE]) — every consumer of
# VixenApp (VIXEN, vixen_editor, offline unit tests, test_fail_scenario_sweep) MUST call this
# instead of a plain `target_link_libraries(<target> ... VixenApp)`, or the scoped node OBJECT
# libs merged into VixenApp.lib get silently dropped at THIS link step (see the EXTRA FIX comment
# above this file's whole-archive helper -- whole-archiving nodes onto VixenApp alone doesn't
# survive VixenApp itself being linked plainly one level up). Defaults to PRIVATE scope (matches
# every current call site's prior `target_link_libraries(<target> PRIVATE VixenApp)`).
function(vixen_whole_archive_link_vixen_app target_name)
    set(_scope PRIVATE)
    if(ARGC GREATER 1)
        set(_scope ${ARGV1})
    endif()
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
        target_link_libraries(${target_name} ${_scope}
            "$<LINK_LIBRARY:WHOLE_ARCHIVE,VixenApp>")
    elseif(MSVC)
        target_link_libraries(${target_name} ${_scope} VixenApp)
        target_link_options(${target_name} ${_scope}
            "/WHOLEARCHIVE:$<TARGET_FILE:VixenApp>")
    else()
        target_link_libraries(${target_name} ${_scope}
            "-Wl,--whole-archive" VixenApp "-Wl,--no-whole-archive")
    endif()
endfunction()
