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

    # --- Hud [View] schema via the same Yeroket tool (View Contract Inc-2: renamed from EditorHud).
    # The generated header lives under application/main — it is a CONSUMER artifact (the native
    # HudView), not an engine one; the engine only sees the generic Ui/IView.h seam.
    set(_view_args
        run --project "${_yk_tool}" -c Release --
        --schema "${_cg}/view-schemas" --view Hud
        --out-header "${CMAKE_SOURCE_DIR}/application/main/include/Generated/Hud.g.h")
    add_custom_target(view_hud_check ALL
        COMMAND ${VIXEN_DOTNET} ${_view_args} --check
        COMMENT "[codegen] golden check: Hud.g.h matches canonical [View] schema (Yeroket tool)"
        VERBATIM)
    add_custom_target(view_hud_regen
        COMMAND ${VIXEN_DOTNET} ${_view_args}
        COMMENT "[codegen] regenerate Hud.g.h (Yeroket tool)"
        VERBATIM)

    # --- Hud RML data-binding partial (View Contract Inc-2, Task 8: RmlMarkupEmitter's --view-markup
    # sibling of --view above). Hud.view.rml is a committed, generated FRAGMENT (not a full document)
    # composed by hand into hud.rml's marked GEN:hud-bindings region — this target only guards the
    # fragment itself against schema drift; the composed hud.rml is proven live by
    # test_hud_render_capture (the real-GPU byte-exact gate), not a second textual diff here.
    set(_view_markup_args
        run --project "${_yk_tool}" -c Release --
        --schema "${_cg}/view-schemas" --view-markup Hud
        --out-rml "${CMAKE_SOURCE_DIR}/libraries/RenderGraph/assets/ui/Hud.view.rml")
    add_custom_target(view_hud_markup_check ALL
        COMMAND ${VIXEN_DOTNET} ${_view_markup_args} --check
        COMMENT "[codegen] golden check: Hud.view.rml matches canonical [View] schema (Yeroket tool)"
        VERBATIM)
    add_custom_target(view_hud_markup_regen
        COMMAND ${VIXEN_DOTNET} ${_view_markup_args}
        COMMENT "[codegen] regenerate Hud.view.rml (Yeroket tool)"
        VERBATIM)

    # --- Hud reflection blob (View Contract Inc-2b): --view-blob emits a constexpr C++ header
    # (the generic BlobView's compile-time delivery) AND a runtime .viewblob data file. Both are
    # committed + drift-guarded against the canonical [View] schema, like every other generated artifact.
    set(_view_blob_args
        run --project "${_yk_tool}" -c Release --
        --schema "${_cg}/view-schemas" --view-blob Hud
        --out-header  "${CMAKE_SOURCE_DIR}/application/main/include/Generated/Hud.blob.g.h"
        --out-datafile "${CMAKE_SOURCE_DIR}/libraries/RenderGraph/assets/ui/hud.viewblob")
    add_custom_target(view_hud_blob_check ALL
        COMMAND ${VIXEN_DOTNET} ${_view_blob_args} --check
        COMMENT "[codegen] golden check: Hud.blob.g.h + hud.viewblob match canonical [View] schema"
        VERBATIM)
    add_custom_target(view_hud_blob_regen
        COMMAND ${VIXEN_DOTNET} ${_view_blob_args}
        COMMENT "[codegen] regenerate Hud.blob.g.h + hud.viewblob"
        VERBATIM)
else()
    message(WARNING "[codegen] KI-015: Yeroket kernel-codegen tool not found at ${_yk_tool} — "
        "OctreeConfig/Hud golden DRIFT GUARDS DISABLED; the committed generated artifacts "
        "(OctreeConfig.g.h/.glsl, Hud.g.h, Hud.view.rml, Hud.blob.g.h, hud.viewblob) are used as-is and will NOT be checked against "
        "their canonical schemas this configure. The Yeroket repo is WSL-only on this machine, so a "
        "Windows-side configure cannot reach it — run the WSL-side configure, mount \\\\wsl$, or pass "
        "-DYEROKET_ROOT=<path> to enable the guard.")
endif()
