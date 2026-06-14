# ============================================================================
# VixenInstall.cmake — AR#2: a consumable artifact via find_package(VIXEN)
#
# Centralises install + export of every VIXEN library into ONE unified target
# set (VixenTargets), namespaced Vixen::, plus a generated VIXENConfig.cmake /
# VIXENConfigVersion.cmake. include()d once from the root CMakeLists AFTER
# add_subdirectory(libraries) so all targets exist.
#
# Modern CMake (>=3.13) lets install(TARGETS) reference targets created in
# subdirectories, so the whole machinery lives here rather than being smeared
# across 14 per-library CMakeLists (the previous per-lib *Targets stubs were
# never install(EXPORT)'d, so find_package never worked).
# ============================================================================

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# ----------------------------------------------------------------------------
# AR#2 INCREMENT 2 — third-party closure (REQUIRED before this is consumable).
#
# install(EXPORT) demands every PUBLIC link dependency of an exported target be
# itself exported OR an imported target. With VIXEN_INSTALL_EXPORT=ON the
# generate step currently fails on 9 FetchContent *source* targets that are
# neither (Vulkan/TBB/Threads are imported, so they are already fine — they only
# need find_dependency(), which VIXENConfig.cmake.in already does):
#
#     glm  glfw  nlohmann_json  magic_enum   (have own package configs)
#     stb  VulkanMemoryAllocator  miniz  rmlui_core   (vendored, no config)
#     ProjectHash                                       (VIXEN-internal target)
#
# Two ways to close it (pick per dep):
#   (a) Convert FetchContent -> find_package (imported) in dependencies/, so the
#       dep is imported at configure time and the consumer find_dependency()s it.
#       Right for glm/glfw3/nlohmann_json/magic_enum (real packages).
#   (b) Bundle into this export set (install(TARGETS <dep> EXPORT VixenTargets)
#       + install their headers) — a self-contained SDK. Right for the vendored
#       header/static libs (stb, VMA, miniz, rmlui_core) and ProjectHash.
#
# Until then VIXEN_INSTALL_EXPORT defaults OFF so the normal build stays green.
# ----------------------------------------------------------------------------

# AR#13: the version is owned by the root project(... VERSION x.y.z) — don't hardcode it here.
set(VIXEN_VERSION ${PROJECT_VERSION})

# Target names (note: the Logger target lives in libraries/logger).
set(VIXEN_EXPORTED_TARGETS
    Core
    Logger
    EventBus
    ResourceManagement
    ShaderManagement
    VulkanResources
    CashSystem
    RenderGraph
    VoxelComponents
    VoxelData
    GaiaVoxelWorld
    SVO
    GaiaArchetypes
    Profiler
)

# Source directory names (casing differs from target names for Logger).
set(VIXEN_LIBRARY_DIRS
    Core
    logger
    EventBus
    ResourceManagement
    ShaderManagement
    VulkanResources
    CashSystem
    RenderGraph
    VoxelComponents
    VoxelData
    GaiaVoxelWorld
    SVO
    GaiaArchetypes
    Profiler
)

# 1. Install the static archives and register them in the unified export set.
#    INCLUDES DESTINATION injects $<INSTALL_INTERFACE:include> into each exported
#    target, so consumers get the install-tree include dir automatically.
install(TARGETS ${VIXEN_EXPORTED_TARGETS}
    EXPORT VixenTargets
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# 2. Install each library's public headers. They are FLATTENED into a single
#    include/ prefix because VIXEN libraries include each other by include-dir-
#    relative paths ("Core/NodeInstance.h", "SceneGenerator.h", ...) — exactly
#    how the in-tree build resolves them via PUBLIC include-dir transitivity.
foreach(_dir IN LISTS VIXEN_LIBRARY_DIRS)
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/libraries/${_dir}/include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING
            PATTERN "*.h"
            PATTERN "*.hpp"
            PATTERN "*.inl"
    )
endforeach()

# 2a. The generated <VixenVersion.h> (AR#13; built from project() VERSION in the root
#     CMakeLists) into the same flat include/ prefix, so consumers get it alongside the lib headers.
install(FILES ${CMAKE_BINARY_DIR}/generated/include/VixenVersion.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# 2b. Bundle third-party + internal deps into VixenTargets (fat self-contained SDK).
#     These are PUBLIC — or static-lib PRIVATE, propagated as $<LINK_ONLY:> — deps of
#     VIXEN libraries; being FetchContent/vendored (not imported), install(EXPORT)
#     requires them in an export set. Bundling them + their headers means a consumer
#     needs only Vulkan/TBB found externally. FetchContent lays sources out under
#     ${CMAKE_BINARY_DIR}/_deps/<name>-src.
set(_vx_deps ${CMAKE_BINARY_DIR}/_deps)

# Strip INTERFACE_SOURCES (debugger .natvis files) from the bundled header-only deps.
# nlohmann_json/glm/magic_enum attach a .natvis to their interface; the export bakes that
# path but we don't install it, so a consumer's target_link_libraries fails looking for a
# missing source file. Debug-visualizer only — safe to drop, and gated to the export build.
foreach(_vx_hdr_dep glm glm-header-only nlohmann_json magic_enum)
    if(TARGET ${_vx_hdr_dep})
        set_target_properties(${_vx_hdr_dep} PROPERTIES INTERFACE_SOURCES "")
    endif()
endforeach()

# Header-only / INTERFACE targets (no compiled artifact) + the VIXEN-defined interface libs.
install(TARGETS glm glm-header-only stb VulkanMemoryAllocator magic_enum nlohmann_json ProjectHash
    EXPORT VixenTargets
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
# RmlUi's core library target carries EXPORT_NAME "Core", which would collide with
# Vixen::Core (VIXEN's own Core) under our namespace. Re-name it in the export.
if(TARGET rmlui_core)
    set_target_properties(rmlui_core PROPERTIES EXPORT_NAME RmlUiCore)
endif()

# Compiled static deps (archives).
install(TARGETS glfw miniz rmlui_core
    EXPORT VixenTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
# Their public headers, flattened into <prefix>/include (mirrors the in-tree include dirs).
install(DIRECTORY ${_vx_deps}/glm-src/glm                        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h" PATTERN "*.inl")
install(DIRECTORY ${_vx_deps}/vulkanmemoryallocator-src/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} FILES_MATCHING PATTERN "*.h")
install(DIRECTORY ${_vx_deps}/magic_enum-src/include/            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} FILES_MATCHING PATTERN "*.hpp")
install(DIRECTORY ${_vx_deps}/nlohmann_json-src/include/         DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} FILES_MATCHING PATTERN "*.hpp")
install(DIRECTORY ${_vx_deps}/glfw-src/include/                  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} FILES_MATCHING PATTERN "*.h")
install(DIRECTORY ${_vx_deps}/rmlui-src/Include/                 DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp" PATTERN "*.inl")
# stb / miniz / stbrumme are git checkouts: install ONLY their root headers via an explicit
# glob, so install(DIRECTORY) recursion doesn't drag in .git/docs/tests/examples cruft.
file(GLOB _vx_flat_headers
    ${_vx_deps}/stb-src/*.h
    ${_vx_deps}/miniz-src/*.h
    ${_vx_deps}/stbrumme_hash-src/*.h)
install(FILES ${_vx_flat_headers} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# 3. Write the exported target file (creates the Vixen:: imported targets).
install(EXPORT VixenTargets
    FILE      VixenTargets.cmake
    NAMESPACE Vixen::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/VIXEN
)

# 4. Generate + install the package config and version files.
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/VIXENConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/VIXENConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/VIXEN
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/VIXENConfigVersion.cmake
    VERSION ${VIXEN_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/VIXENConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/VIXENConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/VIXEN
)

message(STATUS "VIXEN: install/export configured — find_package(VIXEN) -> Vixen::* (${VIXEN_VERSION})")
