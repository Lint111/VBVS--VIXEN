#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 5 ]]; then
    echo "Usage: tools/run-vixen-windowed-captures.sh <VIXEN-runtime-dir> <VIXEN-name> <editor-runtime-dir> <editor-name> <capture-output-dir>" >&2
    exit 2
fi

vixen_runtime_dir="$(cd -- "$1" && pwd -P)"
vixen_name="$2"
editor_runtime_dir="$(cd -- "$3" && pwd -P)"
editor_name="$4"
capture_root="$5"
hud_dir="$capture_root/hud"
editor_dir="$capture_root/editor"
mkdir -p "$hud_dir" "$editor_dir"

# Match the runtime library environment configured for CTest in
# VIXEN/cmake/VixenTesting.cmake. This script launches the applications directly,
# so CTest's LD_LIBRARY_PATH property is not inherited automatically.
build_dir="$(dirname -- "$vixen_runtime_dir")"
cache_file="$build_dir/CMakeCache.txt"
cache_value() {
    local key="$1"
    if [[ -r "$cache_file" ]]; then
        awk -F= -v key="$key" '$1 ~ ("^" key ":") { sub(/^[^=]*=/, ""); print; exit }' "$cache_file"
    fi
}

library_arch="$(cache_value CMAKE_LIBRARY_ARCHITECTURE)"
if [[ -z "$library_arch" ]]; then
    library_arch="$(gcc -print-multiarch 2>/dev/null || true)"
fi
if [[ -z "$library_arch" ]]; then
    library_arch="$(uname -m)"
fi

windowing_cache="$(cache_value VIXEN_WINDOWING_CACHE_DIR)"
if [[ -z "$windowing_cache" ]]; then
    windowing_cache="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../VIXEN" && pwd -P)/.windowing-deps"
fi
dzn_library_dir="$(cache_value VIXEN_WSL_DZN_LIBRARY_DIR)"
vulkan_sdk_root="$(cache_value VULKAN_PATH)"
if [[ -z "$vulkan_sdk_root" ]]; then
    vulkan_cache="$(cache_value VIXEN_VULKAN_CACHE_DIR)"
    vulkan_version="$(cache_value VIXEN_VULKAN_SDK_VERSION)"
    if [[ -n "$vulkan_cache" && -n "$vulkan_version" ]]; then
        vulkan_sdk_root="$vulkan_cache/$vulkan_version/x86_64"
    fi
fi

runtime_library_dirs=()
add_library_dir() {
    local candidate="$1"
    [[ -d "$candidate" ]] || return 0
    local existing
    for existing in "${runtime_library_dirs[@]}"; do
        [[ "$existing" == "$candidate" ]] && return 0
    done
    runtime_library_dirs+=("$candidate")
}
add_library_dir "$dzn_library_dir"
add_library_dir "$windowing_cache/usr/lib/$library_arch"
add_library_dir "$windowing_cache/usr/lib"
add_library_dir "$vulkan_sdk_root/lib/VulkanLoader/lib"
add_library_dir "$vulkan_sdk_root/lib"
if [[ -n "${LD_LIBRARY_PATH:-}" ]]; then
    runtime_library_dirs+=("$LD_LIBRARY_PATH")
fi
if [[ ${#runtime_library_dirs[@]} -gt 0 ]]; then
    printf -v capture_library_path '%s:' "${runtime_library_dirs[@]}"
    export LD_LIBRARY_PATH="${capture_library_path%:}"
fi

DISPLAY="${DISPLAY:-:0}"
export DISPLAY
unset WAYLAND_DISPLAY

display_number="${DISPLAY#*:}"
display_number="${display_number%%.*}"
if [[ ! "$display_number" =~ ^[0-9]+$ || ! -S "/tmp/.X11-unix/X${display_number}" ]]; then
    echo "No local X11 socket is available for DISPLAY=$DISPLAY" >&2
    exit 2
fi

vixen="$vixen_runtime_dir/$vixen_name"
editor="$editor_runtime_dir/$editor_name"
for executable in "$vixen" "$editor"; do
    if [[ ! -x "$executable" ]]; then
        echo "Capture executable is missing or not executable: $executable" >&2
        exit 2
    fi
done

run_capture() {
    local name="$1"
    local executable="$2"
    local working_dir="$3"
    local log_path="$4"
    shift 4

    if ! (cd -- "$working_dir" && timeout 180s env -u VIXEN_TEST_CELSHADE_LAMBERT_GGX "$@" "$executable") >"$log_path" 2>&1; then
        echo "$name capture failed; last output from $log_path:" >&2
        tail -n 80 "$log_path" >&2
        return 1
    fi
}

rm -f -- \
    "$hud_dir/hud_capture_5.png" \
    "$hud_dir/hud_capture_45.png" \
    "$hud_dir/hud_capture_75.png" \
    "$editor_dir/editor_capture_5.png" \
    "$editor_dir/editor_capture_45.png" \
    "$editor_dir/editor_capture_75.png" \
    "$editor_dir/editor_capture_105.png"

run_capture "HUD" "$vixen" "$vixen_runtime_dir" "$capture_root/hud.log" \
    "VIXEN_TEST_CELSHADE_LAMBERT_GGX=1" \
    "VIXEN_HUD_SCRIPT=A@30,B@60" \
    "VIXEN_HUD_CAPTURE_FRAMES=5,45,75" \
    "VIXEN_HUD_CAPTURE_DIR=$hud_dir" \
    "VIXEN_EXIT_AFTER_FRAMES=85"

run_capture "editor" "$editor" "$editor_runtime_dir" "$capture_root/editor.log" \
    "VIXEN_EDITOR_SCRIPT=toggle:2@30,undo@60,redo@90,settings@100,back@110" \
    "VIXEN_EDITOR_CAPTURE_FRAMES=5,45,75,105" \
    "VIXEN_EDITOR_CAPTURE_DIR=$editor_dir" \
    "VIXEN_EDITOR_TEST_CAMERA=top-down" \
    "VIXEN_EXIT_AFTER_FRAMES=120"

for image in \
    "$hud_dir/hud_capture_5.png" \
    "$hud_dir/hud_capture_45.png" \
    "$hud_dir/hud_capture_75.png" \
    "$editor_dir/editor_capture_5.png" \
    "$editor_dir/editor_capture_45.png" \
    "$editor_dir/editor_capture_75.png" \
    "$editor_dir/editor_capture_105.png"; do
    if [[ ! -s "$image" ]]; then
        echo "Expected native capture was not produced: $image" >&2
        exit 1
    fi
done

echo "Native VIXEN captures written to $capture_root (DISPLAY=$DISPLAY)"
