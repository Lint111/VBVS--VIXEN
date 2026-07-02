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
