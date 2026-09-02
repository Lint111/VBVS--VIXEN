#!/usr/bin/env bash
# wslgfix — WSLg windowed acceptance run (ruling 0ep.3).
#
# Proves: a windowed VIXEN target OPENS A WINDOW and PRESENTS >=60 frames under WSLg, exits clean,
# and NAMES which ICD actually loaded — read from DEVICE PROPERTIES, never inferred from the env.
#
# Usage:  wslg-acceptance.sh <binary> <dzn|lavapipe> <logfile>
#
# Why the environment must be set explicitly (measured 2026-09-02, see 2026-09-02-wslgfix.md §3):
#   * A dispatched agent shell inherits NO DISPLAY and NO WAYLAND_DISPLAY even though WSLg is live
#     (weston running; Xwayland on /tmp/.X11-unix/X0). Without DISPLAY the run dies at window
#     creation, which looks nothing like the failure it actually is.
#   * There is NO dzn ICD in the system Vulkan path — Ubuntu's mesa-vulkan-drivers ships no
#     libvulkan_dzn.so. Dozen exists ONLY in ~/.cache/vixen/wsl-vulkan (built from source by
#     cmake/ProvisionWslVulkan.cmake). With VK_ICD_FILENAMES unset the loader SILENTLY selects
#     lavapipe (llvmpipe, a CPU rasterizer) — a green run that proves nothing about the GPU path.
set -uo pipefail

BIN="${1:?usage: wslg-acceptance.sh <binary> <dzn|lavapipe> <logfile>}"
MODE="${2:?mode must be dzn or lavapipe}"
OUT="${3:?logfile required}"

DZN_ICD="$HOME/.cache/vixen/wsl-vulkan/dzn_icd.json"
FRAMES="${FRAMES:-90}"          # >60 with margin, so a short stall still clears the bar

export DISPLAY="${DISPLAY:-:0}"
export VIXEN_EXIT_AFTER_FRAMES="$FRAMES"
export VIXEN_LOG_LEVEL=INFO

case "$MODE" in
  dzn)
    if [ ! -f "$DZN_ICD" ]; then
      echo "FATAL: Dozen manifest absent at $DZN_ICD — configure with VIXEN_AUTO_PROVISION_WSL_VULKAN=ON" >&2
      exit 3
    fi
    export VK_ICD_FILENAMES="$DZN_ICD" ;;
  lavapipe)
    unset VK_ICD_FILENAMES ;;
  *) echo "FATAL: mode must be dzn or lavapipe" >&2; exit 3 ;;
esac

# Preflight: the WSLg display must actually be reachable, not merely configured.
if [ ! -e /tmp/.X11-unix/X0 ] && [ ! -e "${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/wayland-0" ] \
   && [ ! -e /mnt/wslg/runtime-dir/wayland-0 ]; then
  echo "FATAL: no reachable WSLg display (no X0 socket, no wayland-0)" >&2
  exit 4
fi

{
  echo "=== wslgfix WSLg acceptance: mode=$MODE ==="
  echo "date        : $(date -Is)"
  echo "binary      : $BIN"
  echo "DISPLAY     : $DISPLAY"
  echo "VK_ICD_FILENAMES: ${VK_ICD_FILENAMES:-(unset -> loader default)}"
  echo "frames req  : $VIXEN_EXIT_AFTER_FRAMES"
  echo "--- run ---"
} > "$OUT"

timeout 180 "$BIN" >> "$OUT" 2>&1
rc=$?                       # captured IMMEDIATELY — before any $(...) can clobber $?
echo "--- end (process exit=$rc) ---" >> "$OUT"

# ---- verdict, from the LOG, not from the exit code alone ----
# The ICD is named by DeviceNode's own "Selected GPU" line (DeviceNode.cpp:236, prints
# props.deviceName): "Microsoft Direct3D12 (...)" => dzn; "llvmpipe" => lavapipe software.
gpu_line=$(grep -a "Selected GPU" "$OUT" | head -1)
neg_line=$(grep -a "Presentation negotiated on queue family" "$OUT" | head -1)
frames_seen=$(grep -aoE "frame[ =]?#?[0-9]+" "$OUT" | grep -oE "[0-9]+" | sort -n | tail -1)

echo
echo "ICD (from device properties): ${gpu_line:-<<NOT FOUND — did the device node run?>>}"
echo "Negotiation                 : ${neg_line:-<<NOT FOUND — SwapChainNode did not negotiate>>}"
echo "Highest frame index seen    : ${frames_seen:-<none>}"
echo "Process exit                : $rc"

if [ "$rc" -ne 0 ]; then echo "VERDICT: FAIL (exit=$rc)"; exit "$rc"; fi
case "$gpu_line" in
  *"Microsoft Direct3D12"*) echo "VERDICT: presented on DZN (real GPU) — 0ep.3 target" ;;
  *llvmpipe*)               echo "VERDICT: presented on LAVAPIPE (software) — FALLBACK, not the acceptance" ;;
  *)                        echo "VERDICT: exit clean but ICD unidentified — do NOT report as acceptance" ;;
esac
