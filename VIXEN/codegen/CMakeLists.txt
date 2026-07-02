# Regen + golden gate for Vixen.Codegen. Only active when dotnet is available
# (D8: normal builds compile committed artifacts; only regen needs .NET).
find_program(VIXEN_DOTNET NAMES dotnet HINTS "$ENV{HOME}/.dotnet")
if(NOT VIXEN_DOTNET)
    message(STATUS "[codegen] dotnet not found — skipping regen/golden gate (committed artifacts used as-is)")
    return()
endif()

set(_cg  "${CMAKE_CURRENT_SOURCE_DIR}")
set(_gen "${_cg}/generated")

# Golden gate: fail the build if committed artifacts are stale vs. the canonical schema.
add_custom_target(codegen_check ALL
    COMMAND ${VIXEN_DOTNET} run --project "${_cg}/Vixen.Codegen" -c Release --
            --schema "${_cg}/schemas"
            --out-cpp "${_gen}/SkeletonConfig.g.h"
            --out-glsl "${_gen}/SkeletonConfig.glsl"
            --check
    COMMENT "[codegen] golden check: committed artifacts match canonical schema"
    VERBATIM)

# Convenience regen target (manual): cmake --build <dir> --target codegen_regen
add_custom_target(codegen_regen
    COMMAND ${VIXEN_DOTNET} run --project "${_cg}/Vixen.Codegen" -c Release --
            --schema "${_cg}/schemas"
            --out-cpp "${_gen}/SkeletonConfig.g.h"
            --out-glsl "${_gen}/SkeletonConfig.glsl"
    COMMENT "[codegen] regenerate committed artifacts"
    VERBATIM)

# --- Phase B: OctreeConfig via the Yeroket kernel-codegen tool (D5: config outside Yeroket, tool in Yeroket) ---
set(YEROKET_ROOT "$ENV{HOME}/Github/Yeroket-Fantasy" CACHE PATH "Yeroket repo root (kernel-codegen tool)")
set(_yk_tool "${YEROKET_ROOT}/Packages/com.yeroket.utility.kernel-framework/CodegenTool~")
if(EXISTS "${_yk_tool}/CodegenTool.csproj")
    set(_oc_args
        run --project "${_yk_tool}" -c Release --
        --schema "${_cg}/config-schemas" --struct OctreeConfig
        --out-cpp  "${CMAKE_SOURCE_DIR}/libraries/SVO/include/Generated/OctreeConfig.g.h"
        --out-glsl "${CMAKE_SOURCE_DIR}/shaders/Generated/OctreeConfig.glsl")
    add_custom_target(octreeconfig_check ALL
        COMMAND ${VIXEN_DOTNET} ${_oc_args} --check
        COMMENT "[codegen] golden check: OctreeConfig.g.h/.glsl match canonical (Yeroket tool)"
        VERBATIM)
    add_custom_target(octreeconfig_regen
        COMMAND ${VIXEN_DOTNET} ${_oc_args}
        COMMENT "[codegen] regenerate OctreeConfig.g.h/.glsl (Yeroket tool)"
        VERBATIM)
else()
    message(STATUS "[codegen] Yeroket tool not found at ${_yk_tool} — skipping OctreeConfig golden gate")
endif()
