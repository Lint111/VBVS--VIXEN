# Regen + golden gate for the config-struct codegen. Only active when dotnet is
# available (D8: normal builds compile committed artifacts; only regen needs .NET).
# Canonical schemas live in config-schemas/; codegen is done by the Yeroket
# kernel-codegen tool (D5: the tool lives in Yeroket, config is authored here).
#
# KI-015: both dotnet and the Yeroket repo are resolved from the user home. On a
# WINDOWS-side configure `$ENV{HOME}` is empty (Windows uses USERPROFILE), so a
# bare `$ENV{HOME}/...` hint silently missed and the drift guards were skipped
# WITHOUT WARNING — a Windows build then looked "guarded" while the generated
# headers were never checked. Fix: probe several home candidates for both the
# tool and dotnet, keep the -DYEROKET_ROOT= override, and make EVERY not-found
# path a loud message(WARNING) that names the guard as DISABLED. (The Yeroket
# repo is WSL-only on this machine, so a Windows configure legitimately can't
# reach it — but that must be surfaced, not swallowed.)

# Candidate user-home roots, most-specific first. Any may be empty; EXISTS filters.
set(_home_candidates "$ENV{HOME}" "$ENV{USERPROFILE}")
# On a Windows configure, probe the current user's WSL home so the WSL-side clone
# is still reachable when \\wsl$ is mounted (no-op when it isn't).
if(DEFINED ENV{USERNAME})
    list(APPEND _home_candidates "//wsl$/Ubuntu/home/$ENV{USERNAME}" "//wsl.localhost/Ubuntu/home/$ENV{USERNAME}")
endif()

# Robust dotnet discovery across both OS conventions + common install dirs.
set(_dotnet_hints "")
foreach(_h IN LISTS _home_candidates)
    if(_h)
        list(APPEND _dotnet_hints "${_h}/.dotnet")
    endif()
endforeach()
find_program(VIXEN_DOTNET NAMES dotnet
    HINTS ${_dotnet_hints}
    PATHS "/usr/bin" "/usr/local/bin" "/usr/share/dotnet" "C:/Program Files/dotnet")
if(NOT VIXEN_DOTNET)
    message(WARNING "[codegen] KI-015: dotnet not found (searched ${_dotnet_hints} + system paths) — "
        "codegen regen/golden DRIFT GUARD DISABLED; committed generated headers are used as-is and "
        "will NOT be checked against their canonical schemas this configure. Pass -DVIXEN_DOTNET=<path> "
        "or run the WSL-side configure to enable the guard.")
    return()
endif()

set(_cg "${CMAKE_CURRENT_SOURCE_DIR}")

# --- OctreeConfig via the Yeroket kernel-codegen tool (D5: config outside Yeroket, tool in Yeroket) ---
# Resolve YEROKET_ROOT from the first home candidate that actually contains the repo,
# so a Windows configure with USERPROFILE (or a mounted WSL home) still finds it.
# -DYEROKET_ROOT= overrides the probe entirely.
if(NOT DEFINED YEROKET_ROOT)
    set(_yk_root "")
    foreach(_h IN LISTS _home_candidates)
        if(_h AND EXISTS "${_h}/Github/Yeroket-Fantasy/Packages/com.yeroket.utility.kernel-framework/CodegenTool~/CodegenTool.csproj")
            set(_yk_root "${_h}/Github/Yeroket-Fantasy")
            break()
        endif()
    endforeach()
    # Fall back to the WSL-home default even if the probe missed, so the not-found
    # WARNING below reports a meaningful path rather than an empty string.
    if(NOT _yk_root)
        set(_yk_root "$ENV{HOME}/Github/Yeroket-Fantasy")
    endif()
    set(YEROKET_ROOT "${_yk_root}" CACHE PATH "Yeroket repo root (kernel-codegen tool)")
endif()
set(_yk_tool "${YEROKET_ROOT}/Packages/com.yeroket.utility.kernel-framework/CodegenTool~")
if(EXISTS "${_yk_tool}/CodegenTool.csproj")
    # KI-015 (execution half): the tool + dotnet resolved above can be a WSL-only clone
    # reached via a \\wsl$ mount — that lets CMake *see* the path on a Windows configure,
    # but ninja's cmd.exe cannot *execute* a Linux ELF at a UNC path. Bridge through
    # wsl.exe in that case: it can invoke the WSL-side dotnet from a Windows process.
    # Native configures (WSL-side, or a future native-Windows tool) are untouched.
    set(_codegen_need_wsl_bridge FALSE)
    if(WIN32 AND (VIXEN_DOTNET MATCHES "wsl" OR YEROKET_ROOT MATCHES "wsl"))
        set(_codegen_need_wsl_bridge TRUE)
    endif()

    if(_codegen_need_wsl_bridge)
        find_program(WSL_EXE NAMES wsl wsl.exe)
        if(NOT WSL_EXE)
            message(WARNING "[codegen] KI-015: tool/dotnet resolved to a WSL-only path (${_yk_tool}) "
                "but wsl.exe was not found — codegen regen/golden DRIFT GUARD DISABLED on this "
                "Windows configure; committed generated artifacts are used as-is. Install the "
                "'Windows Subsystem for Linux' feature (wsl.exe on PATH) or run the WSL-side configure.")
            return()
        endif()
        # Resolve the WSL-side dotnet by asking WSL itself, rather than trying to reverse-parse
        # VIXEN_DOTNET's UNC form — deterministic and survives whatever hint matched above.
        execute_process(
            COMMAND ${WSL_EXE} -e bash -lc "command -v dotnet || echo \$HOME/.dotnet/dotnet"
            OUTPUT_VARIABLE _wsl_dotnet
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(NOT _wsl_dotnet)
            message(WARNING "[codegen] KI-015: wsl.exe found but could not resolve a WSL-side dotnet — "
                "codegen regen/golden DRIFT GUARD DISABLED. Run the WSL-side configure to enable it.")
            return()
        endif()
        # ${VIXEN_ROOT}/... args are Windows paths (C:/...) on this configure; the WSL tool
        # needs them as /mnt/c/... — translate once via wslpath and reuse for every target below.
        function(_codegen_to_wsl_path win_path out_var)
            execute_process(
                COMMAND ${WSL_EXE} -e wslpath -u "${win_path}"
                OUTPUT_VARIABLE _translated
                OUTPUT_STRIP_TRAILING_WHITESPACE)
            set(${out_var} "${_translated}" PARENT_SCOPE)
        endfunction()
        _codegen_to_wsl_path("${_yk_tool}" _yk_tool_run)
        _codegen_to_wsl_path("${_cg}/config-schemas" _schema_config_run)
        _codegen_to_wsl_path("${_cg}/view-schemas" _schema_view_run)
        # ESVO Address Extraction Slice V1: its own schema dir, deliberately OUTSIDE the
        # --callable-cpp sweep root (_schema_callables_run = the whole codegen/ dir below) --
        # SharedPrefixLength's explicit CppNamespace="Vixen::SVO" would collide with that sweep's
        # single committed "Vixen::AppFlow::Generated" namespace (Program.cs's
        # BuildCallableCppHeader throws NotSupportedException on a namespace mismatch within one
        # invocation) if it sat under config-schemas/ or view-schemas/. See svo-schemas/EsvoAddress.cs.
        _codegen_to_wsl_path("${_cg}/svo-schemas" _schema_svo_run)
        # Undertow's schema catalogue exists only in the superproject layout. Engine-standalone
        # builds consume the committed AppFlow/ViewNounId headers when this tool is absent.
        _codegen_to_wsl_path("${_cg}/../../../../core/src/Undertow.Authoring/Schema/schemas.json" _schema_catalog_run)
        _codegen_to_wsl_path("${VIXEN_ROOT}/libraries/SVO/include/Generated/OctreeConfig.g.h" _out_octreeconfig_cpp_run)
        _codegen_to_wsl_path("${VIXEN_ROOT}/shaders/Generated/OctreeConfig.glsl" _out_octreeconfig_glsl_run)
        _codegen_to_wsl_path("${VIXEN_ROOT}/libraries/SVO/include/Generated/EsvoAddress.g.h" _out_esvoaddress_cpp_run)
        _codegen_to_wsl_path("${VIXEN_ROOT}/shaders/Generated/EsvoAddress.glsl" _out_esvoaddress_glsl_run)
        _codegen_to_wsl_path("${VIXEN_ROOT}/libraries/SVO/include/Generated/EsvoAddressMath.g.hpp" _out_esvoaddressmath_hdr_run)
        _codegen_to_wsl_path("${VIXEN_ROOT}/libraries/RenderGraph/include/Generated/LightingConfig.g.h" _out_lightingconfig_cpp_run)
        _codegen_to_wsl_path("${VIXEN_ROOT}/shaders/Generated/LightingConfig.glsl" _out_lightingconfig_glsl_run)
        _codegen_to_wsl_path("${VIXEN_ROOT}/libraries/RenderGraph/include/Generated/ShadowConfig.g.h" _out_shadowconfig_cpp_run)
        _codegen_to_wsl_path("${VIXEN_ROOT}/shaders/Generated/ShadowConfig.glsl" _out_shadowconfig_glsl_run)
        _codegen_to_wsl_path("${VIXEN_ROOT}/libraries/RenderGraph/include/Generated/AccumulationConfig.g.h" _out_accumulationconfig_cpp_run)
        _codegen_to_wsl_path("${VIXEN_ROOT}/shaders/Generated/AccumulationConfig.glsl" _out_accumulationconfig_glsl_run)
        _codegen_to_wsl_path("${VIXEN_ROOT}/libraries/RenderGraph/include/Generated/PrevCameraConfig.g.h" _out_prevcameraconfig_cpp_run)
        _codegen_to_wsl_path("${VIXEN_ROOT}/shaders/Generated/PrevCameraConfig.glsl" _out_prevcameraconfig_glsl_run)
        _codegen_to_wsl_path("${VIXEN_ROOT}/libraries/RenderGraph/include/Generated/ReservoirConfig.g.h" _out_reservoirconfig_cpp_run)
        _codegen_to_wsl_path("${VIXEN_ROOT}/shaders/Generated/ReservoirConfig.glsl" _out_reservoirconfig_glsl_run)
        _codegen_to_wsl_path("${VIXEN_ROOT}/libraries/RenderGraph/include/Generated/ProbeGridConfig.g.h" _out_probegridconfig_cpp_run)
        _codegen_to_wsl_path("${VIXEN_ROOT}/shaders/Generated/ProbeGridConfig.glsl" _out_probegridconfig_glsl_run)
        _codegen_to_wsl_path("${VIXEN_ROOT}/libraries/RenderGraph/include/Generated/LightTreeBuffer.g.h" _out_lighttreebuffer_cpp_run)
        _codegen_to_wsl_path("${VIXEN_ROOT}/shaders/Generated/LightTreeBuffer.glsl" _out_lighttreebuffer_glsl_run)
        _codegen_to_wsl_path("${VIXEN_ROOT}/libraries/RenderGraph/include/Generated/ReservoirRecord.g.h" _out_reservoirrecord_cpp_run)
        _codegen_to_wsl_path("${VIXEN_ROOT}/shaders/Generated/ReservoirRecord.glsl" _out_reservoirrecord_glsl_run)
        # Recipe-Load-Tier-Contract M2 (precision tier): dual full/half layout for the recipe
        # render-param block, via the new --struct-precision emitter.
        _codegen_to_wsl_path("${VIXEN_ROOT}/libraries/SVO/include/Recipe/generated/RecipeParams.g.h" _out_recipeparams_cpp_run)
        _codegen_to_wsl_path("${VIXEN_ROOT}/shaders/Generated/RecipeParams.glsl" _out_recipeparams_glsl_run)
        _codegen_to_wsl_path("${VIXEN_ROOT}/application/main/include/Generated/Hud.g.h" _out_hud_header_run)
        _codegen_to_wsl_path("${VIXEN_ROOT}/libraries/RenderGraph/assets/ui/Hud.view.rml" _out_hud_rml_run)
        # Inc-A2: EditorLayers [View] schema — the editor layer view's generated data-model header.
        # Lives under application/editor (a CONSUMER artifact, same "engine sees only Ui/IView.h"
        # convention Hud.g.h's comment above documents).
        _codegen_to_wsl_path("${VIXEN_ROOT}/application/editor/include/Generated/EditorLayers.g.h" _out_editor_layers_header_run)
        _codegen_to_wsl_path("${VIXEN_ROOT}/application/main/include/Generated/Hud.blob.g.h" _out_hud_blob_header_run)
        _codegen_to_wsl_path("${VIXEN_ROOT}/libraries/RenderGraph/assets/ui/hud.viewblob" _out_hud_blob_data_run)
        _codegen_to_wsl_path("${VIXEN_ROOT}/application/main/include/Generated/Hud.view.g.cs" _out_hud_writer_cs_run)
        _codegen_to_wsl_path("${_cg}/appflow-schemas" _schema_appflow_run)
        # Callables sweep is wider than the appflow schema itself (row-deltas link 2/step-7B):
        # UndertowViewCallables.cs lives in view-schemas/, not appflow-schemas/, so --callable-cpp
        # needs the codegen root (parent of both) to pick up both [KernelCallable] sources.
        # --schema is a single recursive dir sweep filtered by attribute name (Program.cs's
        # --callable-cpp branch), so sweeping the wider root is safe -- config-schemas/ has no
        # [KernelCallable] methods and contributes nothing to the emitted header.
        _codegen_to_wsl_path("${_cg}" _schema_callables_run)
        _codegen_to_wsl_path("${VIXEN_ROOT}/libraries/AppFlow/include/generated/AppFlow.g.h" _out_appflow_hdr_run)
        # seam M2a: the typed ViewNounId enum, generated from every [View] field in view-schemas/
        # (struct-qualified members). #included by IViewDataProvider.h, replacing the Inc-A hand-stub.
        _codegen_to_wsl_path("${VIXEN_ROOT}/libraries/AppFlow/include/generated/ViewNounId.g.h" _out_view_noun_hdr_run)
        # --callable-cpp's tool-owned output moved to AppFlowCallables.generated.g.hpp (row-deltas
        # link 2/step-7B): UndertowViewCallables.IdentityString is `string`-typed, and
        # CppMappingTables has no string->C++ mapping (no bare C++ type for it, unlike
        # int/float/bool/uint), so a transpiled `inline string IdentityString(string v)` never
        # compiles -- a genuine tool gap, not fixable from the schema side alone without editing
        # the kernel's CppMappingTables.cs. AppFlowCallables.g.hpp (the well-known path every
        # UndertowHud*.typed.g.h #includes) is now a small, hand-authored, NOT-drift-checked shim
        # (see the file itself) that #includes this tool-owned header and adds the one hand-written
        # Rml::String overload the tool cannot express -- same "consumer defines by hand" precedent
        # as EditorLayersView's activeLayerCountOverride hook.
        _codegen_to_wsl_path("${VIXEN_ROOT}/libraries/AppFlow/include/generated/AppFlowCallables.generated.g.hpp" _out_callables_hdr_run)
        set(_CODEGEN_RUNNER ${WSL_EXE} -e "${_wsl_dotnet}")
    else()
        set(_yk_tool_run "${_yk_tool}")
        set(_schema_config_run "${_cg}/config-schemas")
        set(_schema_view_run "${_cg}/view-schemas")
        set(_schema_svo_run "${_cg}/svo-schemas")
        # See the WSL-bridge block's engine-standalone caveat above.
        set(_schema_catalog_run "${_cg}/../../../../core/src/Undertow.Authoring/Schema/schemas.json")
        set(_out_octreeconfig_cpp_run "${VIXEN_ROOT}/libraries/SVO/include/Generated/OctreeConfig.g.h")
        set(_out_octreeconfig_glsl_run "${VIXEN_ROOT}/shaders/Generated/OctreeConfig.glsl")
        set(_out_esvoaddress_cpp_run "${VIXEN_ROOT}/libraries/SVO/include/Generated/EsvoAddress.g.h")
        set(_out_esvoaddress_glsl_run "${VIXEN_ROOT}/shaders/Generated/EsvoAddress.glsl")
        set(_out_esvoaddressmath_hdr_run "${VIXEN_ROOT}/libraries/SVO/include/Generated/EsvoAddressMath.g.hpp")
        set(_out_lightingconfig_cpp_run "${VIXEN_ROOT}/libraries/RenderGraph/include/Generated/LightingConfig.g.h")
        set(_out_lightingconfig_glsl_run "${VIXEN_ROOT}/shaders/Generated/LightingConfig.glsl")
        set(_out_shadowconfig_cpp_run "${VIXEN_ROOT}/libraries/RenderGraph/include/Generated/ShadowConfig.g.h")
        set(_out_shadowconfig_glsl_run "${VIXEN_ROOT}/shaders/Generated/ShadowConfig.glsl")
        set(_out_accumulationconfig_cpp_run "${VIXEN_ROOT}/libraries/RenderGraph/include/Generated/AccumulationConfig.g.h")
        set(_out_accumulationconfig_glsl_run "${VIXEN_ROOT}/shaders/Generated/AccumulationConfig.glsl")
        set(_out_prevcameraconfig_cpp_run "${VIXEN_ROOT}/libraries/RenderGraph/include/Generated/PrevCameraConfig.g.h")
        set(_out_prevcameraconfig_glsl_run "${VIXEN_ROOT}/shaders/Generated/PrevCameraConfig.glsl")
        set(_out_reservoirconfig_cpp_run "${VIXEN_ROOT}/libraries/RenderGraph/include/Generated/ReservoirConfig.g.h")
        set(_out_reservoirconfig_glsl_run "${VIXEN_ROOT}/shaders/Generated/ReservoirConfig.glsl")
        set(_out_probegridconfig_cpp_run "${VIXEN_ROOT}/libraries/RenderGraph/include/Generated/ProbeGridConfig.g.h")
        set(_out_probegridconfig_glsl_run "${VIXEN_ROOT}/shaders/Generated/ProbeGridConfig.glsl")
        set(_out_lighttreebuffer_cpp_run "${VIXEN_ROOT}/libraries/RenderGraph/include/Generated/LightTreeBuffer.g.h")
        set(_out_lighttreebuffer_glsl_run "${VIXEN_ROOT}/shaders/Generated/LightTreeBuffer.glsl")
        set(_out_reservoirrecord_cpp_run "${VIXEN_ROOT}/libraries/RenderGraph/include/Generated/ReservoirRecord.g.h")
        set(_out_reservoirrecord_glsl_run "${VIXEN_ROOT}/shaders/Generated/ReservoirRecord.glsl")
        set(_out_recipeparams_cpp_run "${VIXEN_ROOT}/libraries/SVO/include/Recipe/generated/RecipeParams.g.h")
        set(_out_recipeparams_glsl_run "${VIXEN_ROOT}/shaders/Generated/RecipeParams.glsl")
        set(_out_hud_header_run "${VIXEN_ROOT}/application/main/include/Generated/Hud.g.h")
        set(_out_hud_rml_run "${VIXEN_ROOT}/libraries/RenderGraph/assets/ui/Hud.view.rml")
        set(_out_editor_layers_header_run "${VIXEN_ROOT}/application/editor/include/Generated/EditorLayers.g.h")
        set(_out_hud_blob_header_run "${VIXEN_ROOT}/application/main/include/Generated/Hud.blob.g.h")
        set(_out_hud_blob_data_run "${VIXEN_ROOT}/libraries/RenderGraph/assets/ui/hud.viewblob")
        set(_out_hud_writer_cs_run "${VIXEN_ROOT}/application/main/include/Generated/Hud.view.g.cs")
        set(_schema_appflow_run "${_cg}/appflow-schemas")
        # See the WSL-bridge branch's comment above: --callable-cpp sweeps the codegen root so it
        # also picks up view-schemas/UndertowViewCallables.cs, not just appflow-schemas/.
        set(_schema_callables_run "${_cg}")
        set(_out_appflow_hdr_run "${VIXEN_ROOT}/libraries/AppFlow/include/generated/AppFlow.g.h")
        # seam M2a: the typed ViewNounId enum (native-path mirror of the WSL-bridge block above).
        set(_out_view_noun_hdr_run "${VIXEN_ROOT}/libraries/AppFlow/include/generated/ViewNounId.g.h")
        # See the WSL-bridge branch's comment above: tool-owned output moved to
        # AppFlowCallables.generated.g.hpp; AppFlowCallables.g.hpp is now a hand-authored shim.
        set(_out_callables_hdr_run "${VIXEN_ROOT}/libraries/AppFlow/include/generated/AppFlowCallables.generated.g.hpp")
        set(_CODEGEN_RUNNER ${VIXEN_DOTNET})
    endif()

    # Build the CodegenTool ONCE per build, then invoke the produced dll directly (task #29,
    # mirroring vixen/codegen/CMakeLists.txt's undertow_codegen_tool pattern). `dotnet run
    # --project` re-enters MSBuild to evaluate + up-to-date-check the tool project on EVERY
    # invocation -- 17 check + 17 regen targets here each paid that toll independently, AND
    # every concurrent pair raced on the SAME project's obj/bin (GenerateDepsFile IOException /
    # MSB4018 on deps.json -- wave-46 composite finals, two failed runs). Building once and
    # invoking the dll directly means these checks never re-enter MSBuild at all, so they can
    # neither race each other nor undertow's own undertow_codegen_tool build.
    if(_codegen_need_wsl_bridge)
        add_custom_target(engine_codegen_tool
            COMMAND ${WSL_EXE} -e "${_wsl_dotnet}" build "${_yk_tool_run}/CodegenTool.csproj" -c Release -nodeReuse:false
            COMMENT "[codegen] building the kernel CodegenTool once (all engine emitter invocations reuse the dll)"
            JOB_POOL msbuild
            VERBATIM)
    else()
        add_custom_target(engine_codegen_tool
            COMMAND ${VIXEN_DOTNET} build "${_yk_tool}/CodegenTool.csproj" -c Release -nodeReuse:false
            COMMENT "[codegen] building the kernel CodegenTool once (all engine emitter invocations reuse the dll)"
            JOB_POOL msbuild
            VERBATIM)
    endif()
    # TFM read from the csproj (not hardcoded) so a kernel retarget doesn't silently point every
    # emitter at a path that no longer exists -- same reasoning as undertow's identical read.
    file(READ "${_yk_tool}/CodegenTool.csproj" _yk_csproj)
    if(_yk_csproj MATCHES "<TargetFramework>([^<]+)</TargetFramework>")
        set(_yk_tfm "${CMAKE_MATCH_1}")
    else()
        set(_yk_tfm "net8.0")
    endif()
    set(_yk_dll "${_yk_tool}/bin/Release/${_yk_tfm}/CodegenTool.dll")
    if(_codegen_need_wsl_bridge)
        _codegen_to_wsl_path("${_yk_dll}" _yk_dll_run)
    else()
        set(_yk_dll_run "${_yk_dll}")
    endif()

    set(_oc_args
        "${_yk_dll_run}"
        --schema "${_schema_config_run}" --struct OctreeConfig
        --out-cpp  "${_out_octreeconfig_cpp_run}"
        --out-glsl "${_out_octreeconfig_glsl_run}")
    add_custom_target(octreeconfig_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_oc_args} --check
        COMMENT "[codegen] golden check: OctreeConfig.g.h/.glsl match canonical (Yeroket tool)"
        JOB_POOL msbuild
        VERBATIM)
    add_custom_target(octreeconfig_regen
        COMMAND ${_CODEGEN_RUNNER} ${_oc_args}
        COMMENT "[codegen] regenerate OctreeConfig.g.h/.glsl (Yeroket tool)"
        JOB_POOL msbuild
        VERBATIM)

    # --- EsvoAddress (ESVO Address Extraction, Slice V1, 2026-08-17): the depth+8-hop [GpuStruct]
    # POD, generated from svo-schemas/EsvoAddress.cs into libraries/SVO/include/Generated/. Kernel
    # slice K1 (undertow 6f317426) proved this exact shape against a test fixture; this is the
    # real schema, same "config lives with its consuming library" convention as OctreeConfig.
    # No shader currently consumes EsvoAddress.glsl -- --out-glsl is a required flag for --struct
    # regardless (Program.cs line ~3237), so it's emitted as a harmless sidecar, same as every
    # other --struct target here.
    set(_ea_args
        "${_yk_dll_run}"
        --schema "${_schema_svo_run}" --struct EsvoAddress
        --out-cpp  "${_out_esvoaddress_cpp_run}"
        --out-glsl "${_out_esvoaddress_glsl_run}")
    add_custom_target(esvoaddress_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_ea_args} --check
        COMMENT "[codegen] golden check: EsvoAddress.g.h/.glsl match canonical (Yeroket tool)"
        JOB_POOL msbuild
        VERBATIM)
    add_custom_target(esvoaddress_regen
        COMMAND ${_CODEGEN_RUNNER} ${_ea_args}
        COMMENT "[codegen] regenerate EsvoAddress.g.h/.glsl (Yeroket tool)"
        JOB_POOL msbuild
        VERBATIM)

    # --- EsvoAddressMath (same slice): SharedPrefixLength's C++ face, via its OWN --callable-cpp
    # invocation scoped to svo-schemas/ only -- NOT folded into the callables_check sweep below,
    # which is committed to the default "Vixen::AppFlow::Generated" namespace and would throw
    # NotSupportedException on EsvoAddressMath's explicit CppNamespace="Vixen::SVO" if it were
    # swept from the same --schema root (Program.cs's BuildCallableCppHeader: one invocation, one
    # namespace). See svo-schemas/EsvoAddress.cs's own header comment.
    set(_eam_args
        "${_yk_dll_run}"
        --schema "${_schema_svo_run}" --callable-cpp
        --out-header "${_out_esvoaddressmath_hdr_run}")
    add_custom_target(esvoaddressmath_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_eam_args} --check
        COMMENT "[codegen] golden check: EsvoAddressMath.g.hpp matches canonical [KernelCallable] schema"
        JOB_POOL msbuild
        VERBATIM)
    add_custom_target(esvoaddressmath_regen
        COMMAND ${_CODEGEN_RUNNER} ${_eam_args}
        COMMENT "[codegen] regenerate EsvoAddressMath.g.hpp"
        JOB_POOL msbuild
        VERBATIM)

    # --- RecipeParams via --struct-precision (Recipe-Load-Tier-Contract M2, precision tier):
    # emits BOTH the full-precision RecipeParams struct and a half-precision RecipeParamsHalf
    # packed layout (packHalf2x16) from the SAME [PrecisionEligible]-annotated schema, so the
    # two can never drift apart. Not ALL (unlike the other config structs above) -- this is a
    # milestone prototype, gated the same opt-in way the rest of this file already gates
    # WSL-bridge-vs-native, so a build without dotnet reachable degrades to WARNING like every
    # other target here, never a hard configure failure.
    set(_rp_args
        "${_yk_dll_run}"
        --schema "${_schema_config_run}" --struct-precision RecipeParams
        --out-cpp  "${_out_recipeparams_cpp_run}"
        --out-glsl "${_out_recipeparams_glsl_run}")
    add_custom_target(recipeparams_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_rp_args} --check
        COMMENT "[codegen] golden check: RecipeParams.g.h/.glsl match canonical (Yeroket tool, --struct-precision)"
        JOB_POOL msbuild
        VERBATIM)
    add_custom_target(recipeparams_regen
        COMMAND ${_CODEGEN_RUNNER} ${_rp_args}
        COMMENT "[codegen] regenerate RecipeParams.g.h/.glsl (Yeroket tool, --struct-precision)"
        JOB_POOL msbuild
        VERBATIM)

    # --- LightingConfig via the same Yeroket tool (Sampled Lighting Inc0 M1: lights
    # become generated, drift-guarded data types). Generated header lives beside
    # RenderGraph — the future Lighting.glsl/BodyInstanceRayMarch.comp consumer's
    # library — mirroring OctreeConfig's "config lives with its consuming library"
    # convention (SVO owns OctreeConfig because the octree is SVO's core data type).
    set(_lc_args
        "${_yk_dll_run}"
        --schema "${_schema_config_run}" --struct LightingConfig
        --out-cpp  "${_out_lightingconfig_cpp_run}"
        --out-glsl "${_out_lightingconfig_glsl_run}")
    add_custom_target(lightingconfig_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_lc_args} --check
        COMMENT "[codegen] golden check: LightingConfig.g.h/.glsl match canonical (Yeroket tool)"
        JOB_POOL msbuild
        VERBATIM)
    add_custom_target(lightingconfig_regen
        COMMAND ${_CODEGEN_RUNNER} ${_lc_args}
        COMMENT "[codegen] regenerate LightingConfig.g.h/.glsl (Yeroket tool)"
        JOB_POOL msbuild
        VERBATIM)

    # --- ShadowConfig via the same Yeroket tool (Sampled Lighting Inc1 M4: shadow-pass
    # compute budget as drift-guarded data). Generated header lives beside RenderGraph,
    # mirroring LightingConfig's own "config lives with its consuming library" convention.
    set(_sc_args
        "${_yk_dll_run}"
        --schema "${_schema_config_run}" --struct ShadowConfig
        --out-cpp  "${_out_shadowconfig_cpp_run}"
        --out-glsl "${_out_shadowconfig_glsl_run}")
    add_custom_target(shadowconfig_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_sc_args} --check
        COMMENT "[codegen] golden check: ShadowConfig.g.h/.glsl match canonical (Yeroket tool)"
        JOB_POOL msbuild
        VERBATIM)
    add_custom_target(shadowconfig_regen
        COMMAND ${_CODEGEN_RUNNER} ${_sc_args}
        COMMENT "[codegen] regenerate ShadowConfig.g.h/.glsl (Yeroket tool)"
        JOB_POOL msbuild
        VERBATIM)

    # --- AccumulationConfig via the same Yeroket tool (Sampled Lighting Inc2 M1: temporal-
    # accumulation compute budget as drift-guarded data). Generated header lives beside
    # RenderGraph, mirroring ShadowConfig's own "config lives with its consuming library" convention.
    set(_ac_args
        "${_yk_dll_run}"
        --schema "${_schema_config_run}" --struct AccumulationConfig
        --out-cpp  "${_out_accumulationconfig_cpp_run}"
        --out-glsl "${_out_accumulationconfig_glsl_run}")
    add_custom_target(accumulationconfig_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_ac_args} --check
        COMMENT "[codegen] golden check: AccumulationConfig.g.h/.glsl match canonical (Yeroket tool)"
        JOB_POOL msbuild
        VERBATIM)
    add_custom_target(accumulationconfig_regen
        COMMAND ${_CODEGEN_RUNNER} ${_ac_args}
        COMMENT "[codegen] regenerate AccumulationConfig.g.h/.glsl (Yeroket tool)"
        JOB_POOL msbuild
        VERBATIM)

    # --- PrevCameraConfig via the same Yeroket tool (Sampled Lighting Inc2 M3: prev-frame
    # camera matrix plumbing for M4's reprojection). Generated header lives beside RenderGraph,
    # mirroring AccumulationConfig's own "config lives with its consuming library" convention.
    set(_pcc_args
        "${_yk_dll_run}"
        --schema "${_schema_config_run}" --struct PrevCameraConfig
        --out-cpp  "${_out_prevcameraconfig_cpp_run}"
        --out-glsl "${_out_prevcameraconfig_glsl_run}")
    add_custom_target(prevcameraconfig_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_pcc_args} --check
        COMMENT "[codegen] golden check: PrevCameraConfig.g.h/.glsl match canonical (Yeroket tool)"
        JOB_POOL msbuild
        VERBATIM)
    add_custom_target(prevcameraconfig_regen
        COMMAND ${_CODEGEN_RUNNER} ${_pcc_args}
        COMMENT "[codegen] regenerate PrevCameraConfig.g.h/.glsl (Yeroket tool)"
        JOB_POOL msbuild
        VERBATIM)

    # --- ReservoirConfig via the same Yeroket tool (Sampled Lighting Inc3 M3: ReSTIR
    # reservoir-sampling compute budget as drift-guarded data). Generated header lives
    # beside RenderGraph, mirroring PrevCameraConfig's own "config lives with its
    # consuming library" convention. Scaffolding for M4/M5 — see ReservoirConfig.cs's
    # file header for the M3-scope note (reservoirEnabled + lightTreeCutThreshold are
    # the only fields consumed this milestone).
    set(_rc_args
        "${_yk_dll_run}"
        --schema "${_schema_config_run}" --struct ReservoirConfig
        --out-cpp  "${_out_reservoirconfig_cpp_run}"
        --out-glsl "${_out_reservoirconfig_glsl_run}")
    add_custom_target(reservoirconfig_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_rc_args} --check
        COMMENT "[codegen] golden check: ReservoirConfig.g.h/.glsl match canonical (Yeroket tool)"
        JOB_POOL msbuild
        VERBATIM)
    add_custom_target(reservoirconfig_regen
        COMMAND ${_CODEGEN_RUNNER} ${_rc_args}
        COMMENT "[codegen] regenerate ReservoirConfig.g.h/.glsl (Yeroket tool)"
        JOB_POOL msbuild
        VERBATIM)

    # --- ProbeGridConfig via the same Yeroket tool (Sampled Lighting Inc4 M2: DDGI
    # probe-grid placement + compute budget as drift-guarded data). Generated header
    # lives beside RenderGraph, mirroring ReservoirConfig's own "config lives with its
    # consuming library" convention. Scaffolding for M3-M6 — see ProbeGridConfig.cs's
    # file header for the M2-scope note (no probe-update shader reads it yet).
    set(_pgc_args
        "${_yk_dll_run}"
        --schema "${_schema_config_run}" --struct ProbeGridConfig
        --out-cpp  "${_out_probegridconfig_cpp_run}"
        --out-glsl "${_out_probegridconfig_glsl_run}")
    add_custom_target(probegridconfig_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_pgc_args} --check
        COMMENT "[codegen] golden check: ProbeGridConfig.g.h/.glsl match canonical (Yeroket tool)"
        JOB_POOL msbuild
        VERBATIM)
    add_custom_target(probegridconfig_regen
        COMMAND ${_CODEGEN_RUNNER} ${_pgc_args}
        COMMENT "[codegen] regenerate ProbeGridConfig.g.h/.glsl (Yeroket tool)"
        JOB_POOL msbuild
        VERBATIM)

    # --- LightTreeBuffer via the same Yeroket tool (Sampled Lighting Inc3 M4: GPU-side
    # mirror of LightTree.h's CPU LightTreeNode, uploaded per-body so RIS candidate
    # generation can sample the SAME mip-cut light-tree the M3 brute-force reference
    # validates against). Generated header lives beside RenderGraph, mirroring
    # ReservoirConfig's own "config lives with its consuming library" convention.
    set(_ltb_args
        "${_yk_dll_run}"
        --schema "${_schema_config_run}" --struct LightTreeBuffer
        --out-cpp  "${_out_lighttreebuffer_cpp_run}"
        --out-glsl "${_out_lighttreebuffer_glsl_run}")
    add_custom_target(lighttreebuffer_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_ltb_args} --check
        COMMENT "[codegen] golden check: LightTreeBuffer.g.h/.glsl match canonical (Yeroket tool)"
        JOB_POOL msbuild
        VERBATIM)
    add_custom_target(lighttreebuffer_regen
        COMMAND ${_CODEGEN_RUNNER} ${_ltb_args}
        COMMENT "[codegen] regenerate LightTreeBuffer.g.h/.glsl (Yeroket tool)"
        JOB_POOL msbuild
        VERBATIM)

    # --- ReservoirRecord via the same Yeroket tool (Sampled Lighting Inc3 M4: per-pixel
    # RIS/ReSTIR reservoir record — ping-ponged CURRENT/PREVIOUS SSBOs, one record per
    # pixel). Generated header lives beside RenderGraph, mirroring LightTreeBuffer's own
    # "config lives with its consuming library" convention.
    set(_rr_args
        "${_yk_dll_run}"
        --schema "${_schema_config_run}" --struct ReservoirRecord
        --out-cpp  "${_out_reservoirrecord_cpp_run}"
        --out-glsl "${_out_reservoirrecord_glsl_run}")
    add_custom_target(reservoirrecord_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_rr_args} --check
        COMMENT "[codegen] golden check: ReservoirRecord.g.h/.glsl match canonical (Yeroket tool)"
        JOB_POOL msbuild
        VERBATIM)
    add_custom_target(reservoirrecord_regen
        COMMAND ${_CODEGEN_RUNNER} ${_rr_args}
        COMMENT "[codegen] regenerate ReservoirRecord.g.h/.glsl (Yeroket tool)"
        JOB_POOL msbuild
        VERBATIM)

    # --- Hud [View] schema via the same Yeroket tool (View Contract Inc-2: renamed from EditorHud).
    # The generated header lives under application/main — it is a CONSUMER artifact (the native
    # HudView), not an engine one; the engine only sees the generic Ui/IView.h seam.
    set(_view_args
        "${_yk_dll_run}"
        --schema "${_schema_view_run}" --view Hud
        --out-header "${_out_hud_header_run}")
    add_custom_target(view_hud_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_view_args} --check
        COMMENT "[codegen] golden check: Hud.g.h matches canonical [View] schema (Yeroket tool)"
        JOB_POOL msbuild
        VERBATIM)
    add_custom_target(view_hud_regen
        COMMAND ${_CODEGEN_RUNNER} ${_view_args}
        COMMENT "[codegen] regenerate Hud.g.h (Yeroket tool)"
        JOB_POOL msbuild
        VERBATIM)

    # --- EditorLayers [View] schema (Inc-A2): the editor layer view's data-model, giving the
    # editor a real DirtyVariable target for the first time (View-Model-Binding-Inc-A2-Plan-
    # 2026-07.md). Only --view is needed here (no markup/blob/writer sibling) — editor.rml is
    # hand-authored data-bound markup, not composed from a generated RML fragment like hud.rml.
    set(_view_editor_layers_args
        "${_yk_dll_run}"
        --schema "${_schema_view_run}" --view EditorLayers
        --out-header "${_out_editor_layers_header_run}")
    add_custom_target(view_editor_layers_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_view_editor_layers_args} --check
        COMMENT "[codegen] golden check: EditorLayers.g.h matches canonical [View] schema (Yeroket tool)"
        JOB_POOL msbuild
        VERBATIM)
    add_custom_target(view_editor_layers_regen
        COMMAND ${_CODEGEN_RUNNER} ${_view_editor_layers_args}
        COMMENT "[codegen] regenerate EditorLayers.g.h (Yeroket tool)"
        JOB_POOL msbuild
        VERBATIM)

    # --- Hud RML data-binding partial (View Contract Inc-2, Task 8: RmlMarkupEmitter's --view-markup
    # sibling of --view above). Hud.view.rml is a committed, generated FRAGMENT (not a full document)
    # composed by hand into hud.rml's marked GEN:hud-bindings region — this target only guards the
    # fragment itself against schema drift; the composed hud.rml is proven live by
    # test_hud_render_capture (the real-GPU byte-exact gate), not a second textual diff here.
    set(_view_markup_args
        "${_yk_dll_run}"
        --schema "${_schema_view_run}" --view-markup Hud
        --out-rml "${_out_hud_rml_run}")
    add_custom_target(view_hud_markup_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_view_markup_args} --check
        COMMENT "[codegen] golden check: Hud.view.rml matches canonical [View] schema (Yeroket tool)"
        JOB_POOL msbuild
        VERBATIM)
    add_custom_target(view_hud_markup_regen
        COMMAND ${_CODEGEN_RUNNER} ${_view_markup_args}
        COMMENT "[codegen] regenerate Hud.view.rml (Yeroket tool)"
        JOB_POOL msbuild
        VERBATIM)

    # --- Hud reflection blob (View Contract Inc-2b): --view-blob emits a constexpr C++ header
    # (the generic BlobView's compile-time delivery) AND a runtime .viewblob data file. Both are
    # committed + drift-guarded against the canonical [View] schema, like every other generated artifact.
    set(_view_blob_args
        "${_yk_dll_run}"
        --schema "${_schema_view_run}" --view-blob Hud
        --out-header  "${_out_hud_blob_header_run}"
        --out-datafile "${_out_hud_blob_data_run}")
    add_custom_target(view_hud_blob_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_view_blob_args} --check
        COMMENT "[codegen] golden check: Hud.blob.g.h + hud.viewblob match canonical [View] schema"
        JOB_POOL msbuild
        VERBATIM)
    add_custom_target(view_hud_blob_regen
        COMMAND ${_CODEGEN_RUNNER} ${_view_blob_args}
        COMMENT "[codegen] regenerate Hud.blob.g.h + hud.viewblob"
        JOB_POOL msbuild
        VERBATIM)

    # --- Hud C# data-upload writer (View Contract Inc-3): --view-writer emits the typed
    # HudViewWriter (Hud.view.g.cs) — the C# upload face. Committed + drift-guarded like every
    # other generated artifact. NOT compiled in VIXEN (no C# build here); exercised by the
    # Yeroket NUnit tests. The C++ round-trip proof (test_view_wire_roundtrip) consumes the same
    # UTVA wire this writer emits.
    set(_view_writer_args
        "${_yk_dll_run}"
        --schema "${_schema_view_run}" --view-writer Hud
        --out-cs "${_out_hud_writer_cs_run}")
    add_custom_target(view_hud_writer_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_view_writer_args} --check
        COMMENT "[codegen] golden check: Hud.view.g.cs matches canonical [View] schema (Yeroket tool)"
        JOB_POOL msbuild
        VERBATIM)
    add_custom_target(view_hud_writer_regen
        COMMAND ${_CODEGEN_RUNNER} ${_view_writer_args}
        COMMENT "[codegen] regenerate Hud.view.g.cs (Yeroket tool)"
        JOB_POOL msbuild
        VERBATIM)

    # --- AppFlow schema (View Contract Inc-4, M1): --appflow generates AppFlow.g.h from
    # AppFlowReference.cs's [Flow*]-attributed enums/classes — retires the Inc-1 hand-authored
    # mirror (TODO(appflow-codegen)). Committed + drift-guarded like every other generated artifact.
    # AppFlow resolves [FlowDataTarget] names against the same complete ordered noun model as
    # ViewNounId: native [View] schemas, matching derived-query views, then runtime-state facets.
    set(_appflow_args
        "${_yk_dll_run}"
        --schema "${_schema_appflow_run}" --appflow
        --view-schema "${_schema_view_run}"
        --schema-json "${_schema_catalog_run}" --view-executor-consumer Undertow.View
        --exclude VectorProof
        --out-header "${_out_appflow_hdr_run}")
    add_custom_target(appflow_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_appflow_args} --check
        COMMENT "[codegen] golden check: AppFlow.g.h matches canonical AppFlowReference schema"
        JOB_POOL msbuild
        VERBATIM)
    add_custom_target(appflow_regen
        COMMAND ${_CODEGEN_RUNNER} ${_appflow_args}
        COMMENT "[codegen] regenerate AppFlow.g.h"
        JOB_POOL msbuild
        VERBATIM)

    # --- ViewNounId enum: one full catalogue-derived noun space. Native [View] fields retain the
    # prefix in native schema order; every selected derived query and every runtime-state view facet
    # follows in catalogue declaration order. AppFlow above consumes this identical model.
    set(_view_noun_args
        "${_yk_dll_run}"
        --view-noun-enum --schema "${_schema_view_run}"
        --schema-json "${_schema_catalog_run}" --view-executor-consumer Undertow.View
        --exclude VectorProof
        --out-header "${_out_view_noun_hdr_run}")
    add_custom_target(view_noun_enum_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_view_noun_args} --check
        COMMENT "[codegen] golden check: ViewNounId.g.h matches the [View] schema nouns"
        JOB_POOL msbuild
        VERBATIM)
    add_custom_target(view_noun_enum_regen
        COMMAND ${_CODEGEN_RUNNER} ${_view_noun_args}
        COMMENT "[codegen] regenerate ViewNounId.g.h"
        JOB_POOL msbuild
        VERBATIM)

    # --- AppFlow callables (Inc-4 reframe R4, design D12/D13): --callable-cpp transplants every
    # [KernelCallable] method body in the AppFlow schema dir to neutral C++ via CppAstVisitor —
    # domain-blind, no [SdfCoreKernel] filter. Proves the kernel transplants LOGIC, not just DATA.
    # Same schema dir as appflow_check; the AppFlow model builder ignores AppFlowCallables.cs
    # (no [Flow*] attrs on it) so AppFlow.g.h stays byte-identical.
    set(_callables_args
        "${_yk_dll_run}"
        --schema "${_schema_callables_run}" --callable-cpp
        --out-header "${_out_callables_hdr_run}")
    add_custom_target(callables_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_callables_args} --check
        COMMENT "[codegen] golden check: AppFlowCallables.generated.g.hpp matches canonical [KernelCallable] schema"
        JOB_POOL msbuild
        VERBATIM)
    add_custom_target(callables_regen
        COMMAND ${_CODEGEN_RUNNER} ${_callables_args}
        COMMENT "[codegen] regenerate AppFlowCallables.generated.g.hpp"
        JOB_POOL msbuild
        VERBATIM)

    # Every check/regen target above invokes ${_yk_dll_run}, so each needs engine_codegen_tool
    # built first. Derived from this directory's own target list (undertow's identical pattern,
    # vixen/codegen/CMakeLists.txt) rather than a hand-maintained name list — a new emitter
    # target picks the dependency up for free.
    get_property(_engine_codegen_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_t IN LISTS _engine_codegen_targets)
        if(NOT _t STREQUAL "engine_codegen_tool")
            add_dependencies(${_t} engine_codegen_tool)
        endif()
    endforeach()

else()
    message(WARNING "[codegen] KI-015: Yeroket kernel-codegen tool not found at ${_yk_tool} — "
        "OctreeConfig/EsvoAddress/EsvoAddressMath/LightingConfig/ShadowConfig/AccumulationConfig/PrevCameraConfig/ReservoirConfig/ProbeGridConfig/LightTreeBuffer/ReservoirRecord/Hud/EditorLayers/AppFlow/ViewNounId golden DRIFT GUARDS DISABLED; the committed generated artifacts "
        "are used as-is and will NOT be checked against "
        "their canonical schemas this configure. The Yeroket repo is WSL-only on this machine, so a "
        "Windows-side configure cannot reach it — run the WSL-side configure, mount \\\\wsl$, or pass "
        "-DYEROKET_ROOT=<path> to enable the guard.")
endif()
