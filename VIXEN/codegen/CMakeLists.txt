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
        _codegen_to_wsl_path("${VIXEN_ROOT}/libraries/SVO/include/Generated/OctreeConfig.g.h" _out_octreeconfig_cpp_run)
        _codegen_to_wsl_path("${VIXEN_ROOT}/shaders/Generated/OctreeConfig.glsl" _out_octreeconfig_glsl_run)
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
        _codegen_to_wsl_path("${VIXEN_ROOT}/libraries/RenderGraph/include/Generated/LightTreeBuffer.g.h" _out_lighttreebuffer_cpp_run)
        _codegen_to_wsl_path("${VIXEN_ROOT}/shaders/Generated/LightTreeBuffer.glsl" _out_lighttreebuffer_glsl_run)
        _codegen_to_wsl_path("${VIXEN_ROOT}/libraries/RenderGraph/include/Generated/ReservoirRecord.g.h" _out_reservoirrecord_cpp_run)
        _codegen_to_wsl_path("${VIXEN_ROOT}/shaders/Generated/ReservoirRecord.glsl" _out_reservoirrecord_glsl_run)
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
        _codegen_to_wsl_path("${VIXEN_ROOT}/libraries/AppFlow/include/generated/AppFlow.g.h" _out_appflow_hdr_run)
        _codegen_to_wsl_path("${VIXEN_ROOT}/libraries/AppFlow/include/generated/AppFlowCallables.g.hpp" _out_callables_hdr_run)
        # View Read-Model Codegen Milestone 5: the 4 Hud-family --readmodel-cpp DTO headers.
        _codegen_to_wsl_path("${VIXEN_ROOT}/application/main/include/Generated/UndertowHud.readmodel.g.h" _out_undertowhud_readmodel_run)
        _codegen_to_wsl_path("${VIXEN_ROOT}/application/main/include/Generated/UndertowHudFactions.readmodel.g.h" _out_undertowhudfactions_readmodel_run)
        _codegen_to_wsl_path("${VIXEN_ROOT}/application/main/include/Generated/UndertowHudEvents.readmodel.g.h" _out_undertowhudevents_readmodel_run)
        _codegen_to_wsl_path("${VIXEN_ROOT}/application/main/include/Generated/UndertowHudInspect.readmodel.g.h" _out_undertowhudinspect_readmodel_run)
        # View Read-Model Codegen Milestone 6: the umbrella include header for the whole
        # UndertowHud.cs [View] family -- --schema-file (a single .cs file, not the
        # directory-scoped --schema) + --out-dir (existence-probe root for each discovered
        # schema's sibling .blob/.typed/.readmodel.g.h files) + --out-header (the umbrella itself).
        _codegen_to_wsl_path("${_cg}/view-schemas/UndertowHud.cs" _schema_undertowhud_file_run)
        _codegen_to_wsl_path("${VIXEN_ROOT}/application/main/include/Generated" _out_undertowhud_gendir_run)
        _codegen_to_wsl_path("${VIXEN_ROOT}/application/main/include/Generated/UndertowHudFamily.umbrella.g.h" _out_undertowhud_umbrella_run)
        set(_CODEGEN_RUNNER ${WSL_EXE} -e "${_wsl_dotnet}")
    else()
        set(_yk_tool_run "${_yk_tool}")
        set(_schema_config_run "${_cg}/config-schemas")
        set(_schema_view_run "${_cg}/view-schemas")
        set(_out_octreeconfig_cpp_run "${VIXEN_ROOT}/libraries/SVO/include/Generated/OctreeConfig.g.h")
        set(_out_octreeconfig_glsl_run "${VIXEN_ROOT}/shaders/Generated/OctreeConfig.glsl")
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
        set(_out_lighttreebuffer_cpp_run "${VIXEN_ROOT}/libraries/RenderGraph/include/Generated/LightTreeBuffer.g.h")
        set(_out_lighttreebuffer_glsl_run "${VIXEN_ROOT}/shaders/Generated/LightTreeBuffer.glsl")
        set(_out_reservoirrecord_cpp_run "${VIXEN_ROOT}/libraries/RenderGraph/include/Generated/ReservoirRecord.g.h")
        set(_out_reservoirrecord_glsl_run "${VIXEN_ROOT}/shaders/Generated/ReservoirRecord.glsl")
        set(_out_hud_header_run "${VIXEN_ROOT}/application/main/include/Generated/Hud.g.h")
        set(_out_hud_rml_run "${VIXEN_ROOT}/libraries/RenderGraph/assets/ui/Hud.view.rml")
        set(_out_editor_layers_header_run "${VIXEN_ROOT}/application/editor/include/Generated/EditorLayers.g.h")
        set(_out_hud_blob_header_run "${VIXEN_ROOT}/application/main/include/Generated/Hud.blob.g.h")
        set(_out_hud_blob_data_run "${VIXEN_ROOT}/libraries/RenderGraph/assets/ui/hud.viewblob")
        set(_out_hud_writer_cs_run "${VIXEN_ROOT}/application/main/include/Generated/Hud.view.g.cs")
        set(_schema_appflow_run "${_cg}/appflow-schemas")
        set(_out_appflow_hdr_run "${VIXEN_ROOT}/libraries/AppFlow/include/generated/AppFlow.g.h")
        set(_out_callables_hdr_run "${VIXEN_ROOT}/libraries/AppFlow/include/generated/AppFlowCallables.g.hpp")
        # View Read-Model Codegen Milestone 5: the 4 Hud-family --readmodel-cpp DTO headers.
        set(_out_undertowhud_readmodel_run "${VIXEN_ROOT}/application/main/include/Generated/UndertowHud.readmodel.g.h")
        set(_out_undertowhudfactions_readmodel_run "${VIXEN_ROOT}/application/main/include/Generated/UndertowHudFactions.readmodel.g.h")
        set(_out_undertowhudevents_readmodel_run "${VIXEN_ROOT}/application/main/include/Generated/UndertowHudEvents.readmodel.g.h")
        set(_out_undertowhudinspect_readmodel_run "${VIXEN_ROOT}/application/main/include/Generated/UndertowHudInspect.readmodel.g.h")
        # View Read-Model Codegen Milestone 6: umbrella include header (native-path mirror
        # of the WSL-bridge block above).
        set(_schema_undertowhud_file_run "${_cg}/view-schemas/UndertowHud.cs")
        set(_out_undertowhud_gendir_run "${VIXEN_ROOT}/application/main/include/Generated")
        set(_out_undertowhud_umbrella_run "${VIXEN_ROOT}/application/main/include/Generated/UndertowHudFamily.umbrella.g.h")
        set(_CODEGEN_RUNNER ${VIXEN_DOTNET})
    endif()

    set(_oc_args
        run --project "${_yk_tool_run}" -c Release --
        --schema "${_schema_config_run}" --struct OctreeConfig
        --out-cpp  "${_out_octreeconfig_cpp_run}"
        --out-glsl "${_out_octreeconfig_glsl_run}")
    add_custom_target(octreeconfig_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_oc_args} --check
        COMMENT "[codegen] golden check: OctreeConfig.g.h/.glsl match canonical (Yeroket tool)"
        VERBATIM)
    add_custom_target(octreeconfig_regen
        COMMAND ${_CODEGEN_RUNNER} ${_oc_args}
        COMMENT "[codegen] regenerate OctreeConfig.g.h/.glsl (Yeroket tool)"
        VERBATIM)

    # --- LightingConfig via the same Yeroket tool (Sampled Lighting Inc0 M1: lights
    # become generated, drift-guarded data types). Generated header lives beside
    # RenderGraph — the future Lighting.glsl/BodyInstanceRayMarch.comp consumer's
    # library — mirroring OctreeConfig's "config lives with its consuming library"
    # convention (SVO owns OctreeConfig because the octree is SVO's core data type).
    set(_lc_args
        run --project "${_yk_tool_run}" -c Release --
        --schema "${_schema_config_run}" --struct LightingConfig
        --out-cpp  "${_out_lightingconfig_cpp_run}"
        --out-glsl "${_out_lightingconfig_glsl_run}")
    add_custom_target(lightingconfig_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_lc_args} --check
        COMMENT "[codegen] golden check: LightingConfig.g.h/.glsl match canonical (Yeroket tool)"
        VERBATIM)
    add_custom_target(lightingconfig_regen
        COMMAND ${_CODEGEN_RUNNER} ${_lc_args}
        COMMENT "[codegen] regenerate LightingConfig.g.h/.glsl (Yeroket tool)"
        VERBATIM)

    # --- ShadowConfig via the same Yeroket tool (Sampled Lighting Inc1 M4: shadow-pass
    # compute budget as drift-guarded data). Generated header lives beside RenderGraph,
    # mirroring LightingConfig's own "config lives with its consuming library" convention.
    set(_sc_args
        run --project "${_yk_tool_run}" -c Release --
        --schema "${_schema_config_run}" --struct ShadowConfig
        --out-cpp  "${_out_shadowconfig_cpp_run}"
        --out-glsl "${_out_shadowconfig_glsl_run}")
    add_custom_target(shadowconfig_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_sc_args} --check
        COMMENT "[codegen] golden check: ShadowConfig.g.h/.glsl match canonical (Yeroket tool)"
        VERBATIM)
    add_custom_target(shadowconfig_regen
        COMMAND ${_CODEGEN_RUNNER} ${_sc_args}
        COMMENT "[codegen] regenerate ShadowConfig.g.h/.glsl (Yeroket tool)"
        VERBATIM)

    # --- AccumulationConfig via the same Yeroket tool (Sampled Lighting Inc2 M1: temporal-
    # accumulation compute budget as drift-guarded data). Generated header lives beside
    # RenderGraph, mirroring ShadowConfig's own "config lives with its consuming library" convention.
    set(_ac_args
        run --project "${_yk_tool_run}" -c Release --
        --schema "${_schema_config_run}" --struct AccumulationConfig
        --out-cpp  "${_out_accumulationconfig_cpp_run}"
        --out-glsl "${_out_accumulationconfig_glsl_run}")
    add_custom_target(accumulationconfig_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_ac_args} --check
        COMMENT "[codegen] golden check: AccumulationConfig.g.h/.glsl match canonical (Yeroket tool)"
        VERBATIM)
    add_custom_target(accumulationconfig_regen
        COMMAND ${_CODEGEN_RUNNER} ${_ac_args}
        COMMENT "[codegen] regenerate AccumulationConfig.g.h/.glsl (Yeroket tool)"
        VERBATIM)

    # --- PrevCameraConfig via the same Yeroket tool (Sampled Lighting Inc2 M3: prev-frame
    # camera matrix plumbing for M4's reprojection). Generated header lives beside RenderGraph,
    # mirroring AccumulationConfig's own "config lives with its consuming library" convention.
    set(_pcc_args
        run --project "${_yk_tool_run}" -c Release --
        --schema "${_schema_config_run}" --struct PrevCameraConfig
        --out-cpp  "${_out_prevcameraconfig_cpp_run}"
        --out-glsl "${_out_prevcameraconfig_glsl_run}")
    add_custom_target(prevcameraconfig_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_pcc_args} --check
        COMMENT "[codegen] golden check: PrevCameraConfig.g.h/.glsl match canonical (Yeroket tool)"
        VERBATIM)
    add_custom_target(prevcameraconfig_regen
        COMMAND ${_CODEGEN_RUNNER} ${_pcc_args}
        COMMENT "[codegen] regenerate PrevCameraConfig.g.h/.glsl (Yeroket tool)"
        VERBATIM)

    # --- ReservoirConfig via the same Yeroket tool (Sampled Lighting Inc3 M3: ReSTIR
    # reservoir-sampling compute budget as drift-guarded data). Generated header lives
    # beside RenderGraph, mirroring PrevCameraConfig's own "config lives with its
    # consuming library" convention. Scaffolding for M4/M5 — see ReservoirConfig.cs's
    # file header for the M3-scope note (reservoirEnabled + lightTreeCutThreshold are
    # the only fields consumed this milestone).
    set(_rc_args
        run --project "${_yk_tool_run}" -c Release --
        --schema "${_schema_config_run}" --struct ReservoirConfig
        --out-cpp  "${_out_reservoirconfig_cpp_run}"
        --out-glsl "${_out_reservoirconfig_glsl_run}")
    add_custom_target(reservoirconfig_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_rc_args} --check
        COMMENT "[codegen] golden check: ReservoirConfig.g.h/.glsl match canonical (Yeroket tool)"
        VERBATIM)
    add_custom_target(reservoirconfig_regen
        COMMAND ${_CODEGEN_RUNNER} ${_rc_args}
        COMMENT "[codegen] regenerate ReservoirConfig.g.h/.glsl (Yeroket tool)"
        VERBATIM)

    # --- LightTreeBuffer via the same Yeroket tool (Sampled Lighting Inc3 M4: GPU-side
    # mirror of LightTree.h's CPU LightTreeNode, uploaded per-body so RIS candidate
    # generation can sample the SAME mip-cut light-tree the M3 brute-force reference
    # validates against). Generated header lives beside RenderGraph, mirroring
    # ReservoirConfig's own "config lives with its consuming library" convention.
    set(_ltb_args
        run --project "${_yk_tool_run}" -c Release --
        --schema "${_schema_config_run}" --struct LightTreeBuffer
        --out-cpp  "${_out_lighttreebuffer_cpp_run}"
        --out-glsl "${_out_lighttreebuffer_glsl_run}")
    add_custom_target(lighttreebuffer_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_ltb_args} --check
        COMMENT "[codegen] golden check: LightTreeBuffer.g.h/.glsl match canonical (Yeroket tool)"
        VERBATIM)
    add_custom_target(lighttreebuffer_regen
        COMMAND ${_CODEGEN_RUNNER} ${_ltb_args}
        COMMENT "[codegen] regenerate LightTreeBuffer.g.h/.glsl (Yeroket tool)"
        VERBATIM)

    # --- ReservoirRecord via the same Yeroket tool (Sampled Lighting Inc3 M4: per-pixel
    # RIS/ReSTIR reservoir record — ping-ponged CURRENT/PREVIOUS SSBOs, one record per
    # pixel). Generated header lives beside RenderGraph, mirroring LightTreeBuffer's own
    # "config lives with its consuming library" convention.
    set(_rr_args
        run --project "${_yk_tool_run}" -c Release --
        --schema "${_schema_config_run}" --struct ReservoirRecord
        --out-cpp  "${_out_reservoirrecord_cpp_run}"
        --out-glsl "${_out_reservoirrecord_glsl_run}")
    add_custom_target(reservoirrecord_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_rr_args} --check
        COMMENT "[codegen] golden check: ReservoirRecord.g.h/.glsl match canonical (Yeroket tool)"
        VERBATIM)
    add_custom_target(reservoirrecord_regen
        COMMAND ${_CODEGEN_RUNNER} ${_rr_args}
        COMMENT "[codegen] regenerate ReservoirRecord.g.h/.glsl (Yeroket tool)"
        VERBATIM)

    # --- Hud [View] schema via the same Yeroket tool (View Contract Inc-2: renamed from EditorHud).
    # The generated header lives under application/main — it is a CONSUMER artifact (the native
    # HudView), not an engine one; the engine only sees the generic Ui/IView.h seam.
    set(_view_args
        run --project "${_yk_tool_run}" -c Release --
        --schema "${_schema_view_run}" --view Hud
        --out-header "${_out_hud_header_run}")
    add_custom_target(view_hud_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_view_args} --check
        COMMENT "[codegen] golden check: Hud.g.h matches canonical [View] schema (Yeroket tool)"
        VERBATIM)
    add_custom_target(view_hud_regen
        COMMAND ${_CODEGEN_RUNNER} ${_view_args}
        COMMENT "[codegen] regenerate Hud.g.h (Yeroket tool)"
        VERBATIM)

    # --- EditorLayers [View] schema (Inc-A2): the editor layer view's data-model, giving the
    # editor a real DirtyVariable target for the first time (View-Model-Binding-Inc-A2-Plan-
    # 2026-07.md). Only --view is needed here (no markup/blob/writer sibling) — editor.rml is
    # hand-authored data-bound markup, not composed from a generated RML fragment like hud.rml.
    set(_view_editor_layers_args
        run --project "${_yk_tool_run}" -c Release --
        --schema "${_schema_view_run}" --view EditorLayers
        --out-header "${_out_editor_layers_header_run}")
    add_custom_target(view_editor_layers_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_view_editor_layers_args} --check
        COMMENT "[codegen] golden check: EditorLayers.g.h matches canonical [View] schema (Yeroket tool)"
        VERBATIM)
    add_custom_target(view_editor_layers_regen
        COMMAND ${_CODEGEN_RUNNER} ${_view_editor_layers_args}
        COMMENT "[codegen] regenerate EditorLayers.g.h (Yeroket tool)"
        VERBATIM)

    # --- Hud RML data-binding partial (View Contract Inc-2, Task 8: RmlMarkupEmitter's --view-markup
    # sibling of --view above). Hud.view.rml is a committed, generated FRAGMENT (not a full document)
    # composed by hand into hud.rml's marked GEN:hud-bindings region — this target only guards the
    # fragment itself against schema drift; the composed hud.rml is proven live by
    # test_hud_render_capture (the real-GPU byte-exact gate), not a second textual diff here.
    set(_view_markup_args
        run --project "${_yk_tool_run}" -c Release --
        --schema "${_schema_view_run}" --view-markup Hud
        --out-rml "${_out_hud_rml_run}")
    add_custom_target(view_hud_markup_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_view_markup_args} --check
        COMMENT "[codegen] golden check: Hud.view.rml matches canonical [View] schema (Yeroket tool)"
        VERBATIM)
    add_custom_target(view_hud_markup_regen
        COMMAND ${_CODEGEN_RUNNER} ${_view_markup_args}
        COMMENT "[codegen] regenerate Hud.view.rml (Yeroket tool)"
        VERBATIM)

    # --- Hud reflection blob (View Contract Inc-2b): --view-blob emits a constexpr C++ header
    # (the generic BlobView's compile-time delivery) AND a runtime .viewblob data file. Both are
    # committed + drift-guarded against the canonical [View] schema, like every other generated artifact.
    set(_view_blob_args
        run --project "${_yk_tool_run}" -c Release --
        --schema "${_schema_view_run}" --view-blob Hud
        --out-header  "${_out_hud_blob_header_run}"
        --out-datafile "${_out_hud_blob_data_run}")
    add_custom_target(view_hud_blob_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_view_blob_args} --check
        COMMENT "[codegen] golden check: Hud.blob.g.h + hud.viewblob match canonical [View] schema"
        VERBATIM)
    add_custom_target(view_hud_blob_regen
        COMMAND ${_CODEGEN_RUNNER} ${_view_blob_args}
        COMMENT "[codegen] regenerate Hud.blob.g.h + hud.viewblob"
        VERBATIM)

    # --- Hud C# data-upload writer (View Contract Inc-3): --view-writer emits the typed
    # HudViewWriter (Hud.view.g.cs) — the C# upload face. Committed + drift-guarded like every
    # other generated artifact. NOT compiled in VIXEN (no C# build here); exercised by the
    # Yeroket NUnit tests. The C++ round-trip proof (test_view_wire_roundtrip) consumes the same
    # UTVA wire this writer emits.
    set(_view_writer_args
        run --project "${_yk_tool_run}" -c Release --
        --schema "${_schema_view_run}" --view-writer Hud
        --out-cs "${_out_hud_writer_cs_run}")
    add_custom_target(view_hud_writer_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_view_writer_args} --check
        COMMENT "[codegen] golden check: Hud.view.g.cs matches canonical [View] schema (Yeroket tool)"
        VERBATIM)
    add_custom_target(view_hud_writer_regen
        COMMAND ${_CODEGEN_RUNNER} ${_view_writer_args}
        COMMENT "[codegen] regenerate Hud.view.g.cs (Yeroket tool)"
        VERBATIM)

    # --- AppFlow schema (View Contract Inc-4, M1): --appflow generates AppFlow.g.h from
    # AppFlowReference.cs's [Flow*]-attributed enums/classes — retires the Inc-1 hand-authored
    # mirror (TODO(appflow-codegen)). Committed + drift-guarded like every other generated artifact.
    # --view-schema (Inc-4 reframe R3, design D16) cross-checks each [FlowDataTarget]'s view-noun
    # string against the real Hud [View] schema at generation time.
    set(_appflow_args
        run --project "${_yk_tool_run}" -c Release --
        --schema "${_schema_appflow_run}" --appflow
        --view-schema "${_schema_view_run}"
        --out-header "${_out_appflow_hdr_run}")
    add_custom_target(appflow_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_appflow_args} --check
        COMMENT "[codegen] golden check: AppFlow.g.h matches canonical AppFlowReference schema"
        VERBATIM)
    add_custom_target(appflow_regen
        COMMAND ${_CODEGEN_RUNNER} ${_appflow_args}
        COMMENT "[codegen] regenerate AppFlow.g.h"
        VERBATIM)

    # --- AppFlow callables (Inc-4 reframe R4, design D12/D13): --callable-cpp transplants every
    # [KernelCallable] method body in the AppFlow schema dir to neutral C++ via CppAstVisitor —
    # domain-blind, no [SdfCoreKernel] filter. Proves the kernel transplants LOGIC, not just DATA.
    # Same schema dir as appflow_check; the AppFlow model builder ignores AppFlowCallables.cs
    # (no [Flow*] attrs on it) so AppFlow.g.h stays byte-identical.
    set(_callables_args
        run --project "${_yk_tool_run}" -c Release --
        --schema "${_schema_appflow_run}" --callable-cpp
        --out-header "${_out_callables_hdr_run}")
    add_custom_target(callables_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_callables_args} --check
        COMMENT "[codegen] golden check: AppFlowCallables.g.hpp matches canonical [KernelCallable] schema"
        VERBATIM)
    add_custom_target(callables_regen
        COMMAND ${_CODEGEN_RUNNER} ${_callables_args}
        COMMENT "[codegen] regenerate AppFlowCallables.g.hpp"
        VERBATIM)

    # --- View Read-Model Codegen Milestone 5 (R6): --readmodel-cpp DTO headers for the 4
    # undertow Hud-family [View] schemas (UndertowHud/HudFactions/HudEvents/HudInspect).
    # Mirrors view_hud_check/view_hud_regen's pattern exactly -- these are the same undertow
    # view-schemas dir (_schema_view_run) the existing UndertowHud*.typed.g.h accessors are
    # generated from (that CLI invocation is currently run manually, not CMake-driven, per
    # Milestone 5's research; this is the first CMake-wired drift guard for the Undertow* Hud
    # schemas). A schema edit that isn't regenerated now fails the build, same as every other
    # generated artifact here.
    set(_readmodel_undertowhud_args
        run --project "${_yk_tool_run}" -c Release --
        --schema "${_schema_view_run}" --readmodel-cpp UndertowHud
        --out-header "${_out_undertowhud_readmodel_run}")
    add_custom_target(readmodel_undertowhud_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_readmodel_undertowhud_args} --check
        COMMENT "[codegen] golden check: UndertowHud.readmodel.g.h matches canonical [View] schema (Yeroket tool)"
        VERBATIM)
    add_custom_target(readmodel_undertowhud_regen
        COMMAND ${_CODEGEN_RUNNER} ${_readmodel_undertowhud_args}
        COMMENT "[codegen] regenerate UndertowHud.readmodel.g.h (Yeroket tool)"
        VERBATIM)

    set(_readmodel_undertowhudfactions_args
        run --project "${_yk_tool_run}" -c Release --
        --schema "${_schema_view_run}" --readmodel-cpp UndertowHudFactions
        --out-header "${_out_undertowhudfactions_readmodel_run}")
    add_custom_target(readmodel_undertowhudfactions_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_readmodel_undertowhudfactions_args} --check
        COMMENT "[codegen] golden check: UndertowHudFactions.readmodel.g.h matches canonical [View] schema (Yeroket tool)"
        VERBATIM)
    add_custom_target(readmodel_undertowhudfactions_regen
        COMMAND ${_CODEGEN_RUNNER} ${_readmodel_undertowhudfactions_args}
        COMMENT "[codegen] regenerate UndertowHudFactions.readmodel.g.h (Yeroket tool)"
        VERBATIM)

    set(_readmodel_undertowhudevents_args
        run --project "${_yk_tool_run}" -c Release --
        --schema "${_schema_view_run}" --readmodel-cpp UndertowHudEvents
        --out-header "${_out_undertowhudevents_readmodel_run}")
    add_custom_target(readmodel_undertowhudevents_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_readmodel_undertowhudevents_args} --check
        COMMENT "[codegen] golden check: UndertowHudEvents.readmodel.g.h matches canonical [View] schema (Yeroket tool)"
        VERBATIM)
    add_custom_target(readmodel_undertowhudevents_regen
        COMMAND ${_CODEGEN_RUNNER} ${_readmodel_undertowhudevents_args}
        COMMENT "[codegen] regenerate UndertowHudEvents.readmodel.g.h (Yeroket tool)"
        VERBATIM)

    set(_readmodel_undertowhudinspect_args
        run --project "${_yk_tool_run}" -c Release --
        --schema "${_schema_view_run}" --readmodel-cpp UndertowHudInspect
        --out-header "${_out_undertowhudinspect_readmodel_run}")
    add_custom_target(readmodel_undertowhudinspect_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_readmodel_undertowhudinspect_args} --check
        COMMENT "[codegen] golden check: UndertowHudInspect.readmodel.g.h matches canonical [View] schema (Yeroket tool)"
        VERBATIM)
    add_custom_target(readmodel_undertowhudinspect_regen
        COMMAND ${_CODEGEN_RUNNER} ${_readmodel_undertowhudinspect_args}
        COMMENT "[codegen] regenerate UndertowHudInspect.readmodel.g.h (Yeroket tool)"
        VERBATIM)

    # --- View Read-Model Codegen Milestone 6: umbrella include header for the whole
    # UndertowHud.cs [View] family (UndertowHud/HudFactions/HudEvents/HudInspect/Bodies/
    # Recipes). --schema-file discovers every [View] type in that ONE .cs file via
    # CompilationLoader.LoadViews (not the directory-scoped --schema every other target uses
    # here) -- this is what gives "add a new [View] struct to UndertowHud.cs, next regen picks
    # it up with zero CMakeLists edits" (Milestone 6 Task 1 research). --out-dir is the
    # existence-probe root: the umbrella only #includes a schema's .blob/.typed/.readmodel.g.h
    # sibling if that file actually exists there (UndertowBodies/UndertowRecipes have no
    # .readmodel.g.h yet -- Milestone 5 was Hud-only scope -- so the umbrella must degrade
    # gracefully per schema rather than assume all 3 siblings exist for every family member).
    #
    # add_dependencies below is the one genuinely new wrinkle vs. every earlier _check/_regen
    # pair in this file: the umbrella's existence-probe must see a FRESH, consistent set of
    # sibling files, not stale ones from a previous schema shape, so its check/regen must run
    # strictly after all 6 family members' own check/regen targets (the individual schema
    # targets above never needed this -- they have no siblings to depend on).
    set(_readmodel_umbrella_undertowhud_args
        run --project "${_yk_tool_run}" -c Release --
        --readmodel-umbrella-cpp
        --schema-file "${_schema_undertowhud_file_run}"
        --out-dir "${_out_undertowhud_gendir_run}"
        --out-header "${_out_undertowhud_umbrella_run}")
    add_custom_target(readmodel_umbrella_undertowhud_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_readmodel_umbrella_undertowhud_args} --check
        COMMENT "[codegen] golden check: UndertowHudFamily.umbrella.g.h matches canonical [View] schemas in UndertowHud.cs (Yeroket tool)"
        VERBATIM)
    add_custom_target(readmodel_umbrella_undertowhud_regen
        COMMAND ${_CODEGEN_RUNNER} ${_readmodel_umbrella_undertowhud_args}
        COMMENT "[codegen] regenerate UndertowHudFamily.umbrella.g.h (Yeroket tool)"
        VERBATIM)
    # Only the 4 Hud-family schemas have their own CMake _check targets today (Milestone 5's
    # scope; UndertowBodies/UndertowRecipes have no CMake-wired --readmodel-cpp or other
    # per-schema check target at all -- confirmed by Milestone 6 Task 1's research, "no prior
    # CMake target existed for any Undertow* schema check before this milestone" -- so those
    # 2 have nothing to depend on here; the umbrella's own existence-probe still degrades
    # gracefully for them via --out-dir, it just can't order itself after a target that
    # doesn't exist).
    add_dependencies(readmodel_umbrella_undertowhud_check
        readmodel_undertowhud_check readmodel_undertowhudfactions_check
        readmodel_undertowhudevents_check readmodel_undertowhudinspect_check)
else()
    message(WARNING "[codegen] KI-015: Yeroket kernel-codegen tool not found at ${_yk_tool} — "
        "OctreeConfig/LightingConfig/ShadowConfig/AccumulationConfig/PrevCameraConfig/ReservoirConfig/LightTreeBuffer/ReservoirRecord/Hud/EditorLayers/UndertowHud-readmodel/UndertowHudFamily-umbrella golden DRIFT GUARDS DISABLED; the committed generated artifacts "
        "(OctreeConfig.g.h/.glsl, LightingConfig.g.h/.glsl, ShadowConfig.g.h/.glsl, AccumulationConfig.g.h/.glsl, PrevCameraConfig.g.h/.glsl, ReservoirConfig.g.h/.glsl, LightTreeBuffer.g.h/.glsl, ReservoirRecord.g.h/.glsl, Hud.g.h, Hud.view.rml, Hud.blob.g.h, hud.viewblob, EditorLayers.g.h, UndertowHud/HudFactions/HudEvents/HudInspect.readmodel.g.h, UndertowHudFamily.umbrella.g.h) are used as-is and will NOT be checked against "
        "their canonical schemas this configure. The Yeroket repo is WSL-only on this machine, so a "
        "Windows-side configure cannot reach it — run the WSL-side configure, mount \\\\wsl$, or pass "
        "-DYEROKET_ROOT=<path> to enable the guard.")
endif()
