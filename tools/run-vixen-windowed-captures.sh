#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "Usage: tools/run-vixen-windowed-captures.sh <runtime-output-dir> <capture-output-dir>" >&2
    exit 2
fi

runtime_dir="$(cd -- "$1" && pwd -P)"
capture_root="$2"
hud_dir="$capture_root/hud"
editor_dir="$capture_root/editor"
mkdir -p "$hud_dir" "$editor_dir"

DISPLAY="${DISPLAY:-:0}"
export DISPLAY
unset WAYLAND_DISPLAY

display_number="${DISPLAY#*:}"
display_number="${display_number%%.*}"
if [[ ! "$display_number" =~ ^[0-9]+$ || ! -S "/tmp/.X11-unix/X${display_number}" ]]; then
    echo "No local X11 socket is available for DISPLAY=$DISPLAY" >&2
    exit 2
fi

vixen="$runtime_dir/VIXEN"
editor="$runtime_dir/vixen_editor"
for executable in "$vixen" "$editor"; do
    if [[ ! -x "$executable" ]]; then
        echo "Capture executable is missing or not executable: $executable" >&2
        exit 2
    fi
done

run_capture() {
    local name="$1"
    local executable="$2"
    local log_path="$3"
    shift 3

    if ! (cd -- "$runtime_dir" && timeout 180s env "$@" "$executable") >"$log_path" 2>&1; then
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

run_capture "HUD" "$vixen" "$capture_root/hud.log" \
    "VIXEN_HUD_SCRIPT=A@30,B@60" \
    "VIXEN_HUD_CAPTURE_FRAMES=5,45,75" \
    "VIXEN_HUD_CAPTURE_DIR=$hud_dir" \
    "VIXEN_EXIT_AFTER_FRAMES=85"

run_capture "editor" "$editor" "$capture_root/editor.log" \
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
