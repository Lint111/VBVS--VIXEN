# VixenSharedCache.cmake
#
# Root of the machine-wide, content-keyed dependency cache shared by every VIXEN worktree:
#   ${XDG_CACHE_HOME:-$HOME/.cache}/vixen/<dep>/<version-or-content-key>
# (the same root ProvisionWslVulkan.cmake uses for Dozen). Each provisioner populates an entry in a
# temp sibling dir under a per-entry lock, writes a ".vixen-complete" stamp, then renames it into
# place, so a reader never sees a half-populated entry.

include_guard(GLOBAL)

set(VIXEN_SHARED_CACHE_STAMP ".vixen-complete")

function(vixen_shared_cache_root _out)
    if(DEFINED ENV{XDG_CACHE_HOME} AND NOT "$ENV{XDG_CACHE_HOME}" STREQUAL "")
        set(_root "$ENV{XDG_CACHE_HOME}/vixen")
    else()
        set(_root "$ENV{HOME}/.cache/vixen")
    endif()
    set(${_out} "${_root}" PARENT_SCOPE)
endfunction()
