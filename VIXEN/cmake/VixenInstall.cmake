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

set(VIXEN_VERSION 0.1.0)

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
