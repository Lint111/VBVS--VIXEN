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
        # ${CMAKE_SOURCE_DIR}/... args are Windows paths (C:/...) on this configure; the WSL tool
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
        _codegen_to_wsl_path("${CMAKE_SOURCE_DIR}/libraries/SVO/include/Generated/OctreeConfig.g.h" _out_octreeconfig_cpp_run)
        _codegen_to_wsl_path("${CMAKE_SOURCE_DIR}/shaders/Generated/OctreeConfig.glsl" _out_octreeconfig_glsl_run)
        _codegen_to_wsl_path("${CMAKE_SOURCE_DIR}/application/main/include/Generated/Hud.g.h" _out_hud_header_run)
        _codegen_to_wsl_path("${CMAKE_SOURCE_DIR}/libraries/RenderGraph/assets/ui/Hud.view.rml" _out_hud_rml_run)
        _codegen_to_wsl_path("${CMAKE_SOURCE_DIR}/application/main/include/Generated/Hud.blob.g.h" _out_hud_blob_header_run)
        _codegen_to_wsl_path("${CMAKE_SOURCE_DIR}/libraries/RenderGraph/assets/ui/hud.viewblob" _out_hud_blob_data_run)
        _codegen_to_wsl_path("${CMAKE_SOURCE_DIR}/application/main/include/Generated/Hud.view.g.cs" _out_hud_writer_cs_run)
        _codegen_to_wsl_path("${_cg}/appflow-schemas" _schema_appflow_run)
        _codegen_to_wsl_path("${CMAKE_SOURCE_DIR}/libraries/AppFlow/include/generated/AppFlow.g.h" _out_appflow_hdr_run)
        set(_CODEGEN_RUNNER ${WSL_EXE} -e "${_wsl_dotnet}")
    else()
        set(_yk_tool_run "${_yk_tool}")
        set(_schema_config_run "${_cg}/config-schemas")
        set(_schema_view_run "${_cg}/view-schemas")
        set(_out_octreeconfig_cpp_run "${CMAKE_SOURCE_DIR}/libraries/SVO/include/Generated/OctreeConfig.g.h")
        set(_out_octreeconfig_glsl_run "${CMAKE_SOURCE_DIR}/shaders/Generated/OctreeConfig.glsl")
        set(_out_hud_header_run "${CMAKE_SOURCE_DIR}/application/main/include/Generated/Hud.g.h")
        set(_out_hud_rml_run "${CMAKE_SOURCE_DIR}/libraries/RenderGraph/assets/ui/Hud.view.rml")
        set(_out_hud_blob_header_run "${CMAKE_SOURCE_DIR}/application/main/include/Generated/Hud.blob.g.h")
        set(_out_hud_blob_data_run "${CMAKE_SOURCE_DIR}/libraries/RenderGraph/assets/ui/hud.viewblob")
        set(_out_hud_writer_cs_run "${CMAKE_SOURCE_DIR}/application/main/include/Generated/Hud.view.g.cs")
        set(_schema_appflow_run "${_cg}/appflow-schemas")
        set(_out_appflow_hdr_run "${CMAKE_SOURCE_DIR}/libraries/AppFlow/include/generated/AppFlow.g.h")
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
    set(_appflow_args
        run --project "${_yk_tool_run}" -c Release --
        --schema "${_schema_appflow_run}" --appflow
        --out-header "${_out_appflow_hdr_run}")
    add_custom_target(appflow_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_appflow_args} --check
        COMMENT "[codegen] golden check: AppFlow.g.h matches canonical AppFlowReference schema"
        VERBATIM)
    add_custom_target(appflow_regen
        COMMAND ${_CODEGEN_RUNNER} ${_appflow_args}
        COMMENT "[codegen] regenerate AppFlow.g.h"
        VERBATIM)
else()
    message(WARNING "[codegen] KI-015: Yeroket kernel-codegen tool not found at ${_yk_tool} — "
        "OctreeConfig/Hud golden DRIFT GUARDS DISABLED; the committed generated artifacts "
        "(OctreeConfig.g.h/.glsl, Hud.g.h, Hud.view.rml, Hud.blob.g.h, hud.viewblob) are used as-is and will NOT be checked against "
        "their canonical schemas this configure. The Yeroket repo is WSL-only on this machine, so a "
        "Windows-side configure cannot reach it — run the WSL-side configure, mount \\\\wsl$, or pass "
        "-DYEROKET_ROOT=<path> to enable the guard.")
endif()
