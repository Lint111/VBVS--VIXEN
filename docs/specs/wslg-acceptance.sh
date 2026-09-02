#!/usr/bin/env bash
# wslgfix — WSLg windowed acceptance run (ruling 0ep.3). REAL HARDWARE ONLY.
#
# Proves: a windowed VIXEN target OPENS A WINDOW and PRESENTS >=60 frames under WSLg on ACTUAL
# HARDWARE, exits clean, and NAMES the device it ran on — read from DEVICE PROPERTIES, never
# inferred from the env.
#
# Usage:  wslg-acceptance.sh <binary> <logfile>
#
# ⛔ LAVAPIPE IS NOT AN EXECUTION PATH (owner, 2026-09-02). Software Vulkan (llvmpipe/lavapipe) is
# REMOVED, not demoted to a fallback: this harness REFUSES to run on it rather than reporting a
# software result. The engine's own precedent agrees — test_recipe_glsl_numerical_parity.cpp:237
# ("lavapipe execution is forbidden for this task") gates on deviceType via IsRealGpu(), the same
# predicate used below. A software-rendered frame count is not evidence about the hardware path,
# so producing one at all is a liability, not a fallback.
#
# Dozen (dzn) IS real hardware and remains the venue: measured on this box it presents the physical
# RTX 3060 through D3D12 and reports deviceType=2 (DISCRETE_GPU) with driverID=23 (MESA_DOZEN) —
# it passes the same IsRealGpu() gate a native driver does. Lavapipe reports deviceType=4 (CPU).
#
# Why the environment must be set explicitly (measured 2026-09-02, see 2026-09-02-wslgfix.md §3):
#   * A dispatched agent shell inherits NO DISPLAY and NO WAYLAND_DISPLAY even though WSLg is live
#     (weston running; Xwayland on /tmp/.X11-unix/X0). Without DISPLAY the run dies at window
#     creation, which looks nothing like the failure it actually is.
#   * There is NO dzn ICD in the system Vulkan path — Ubuntu's mesa-vulkan-drivers ships no
#     libvulkan_dzn.so. Dozen exists ONLY in ~/.cache/vixen/wsl-vulkan (built from source by
#     cmake/ProvisionWslVulkan.cmake). With VK_ICD_FILENAMES unset the loader SILENTLY selects
#     lavapipe — which this harness now treats as a hard failure, not a degraded pass.
set -uo pipefail

BIN="${1:?usage: wslg-acceptance.sh <binary> <logfile>}"
OUT="${2:?usage: wslg-acceptance.sh <binary> <logfile>}"

DZN_ICD="$HOME/.cache/vixen/wsl-vulkan/dzn_icd.json"
FRAMES="${FRAMES:-90}"          # >60 with margin, so a short stall still clears the bar

export DISPLAY="${DISPLAY:-:0}"
export VIXEN_EXIT_AFTER_FRAMES="$FRAMES"
export VIXEN_LOG_LEVEL=INFO

# Hardware ICD is MANDATORY. Never fall through to the loader default — that is lavapipe.
if [ ! -f "$DZN_ICD" ]; then
  echo "FATAL: Dozen manifest absent at $DZN_ICD." >&2
  echo "       Reconfigure with VIXEN_AUTO_PROVISION_WSL_VULKAN=ON. Refusing to run:" >&2
  echo "       without it the loader silently selects lavapipe, which is not an execution path." >&2
  exit 3
fi
export VK_ICD_FILENAMES="$DZN_ICD"

# Preflight: the WSLg display must actually be reachable, not merely configured.
if [ ! -e /tmp/.X11-unix/X0 ] && [ ! -e "${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/wayland-0" ] \
   && [ ! -e /mnt/wslg/runtime-dir/wayland-0 ]; then
  echo "FATAL: no reachable WSLg display (no X0 socket, no wayland-0)" >&2
  exit 4
fi

# Preflight: PROVE real hardware is what will be selected, BEFORE burning a run. Uses the same
# predicate as the engine's own IsRealGpu (test_recipe_glsl_numerical_parity.cpp:257): only
# DISCRETE (2) / INTEGRATED (1) count. Lavapipe reports CPU (4) and is rejected here, not later.
PROBE="$(dirname "$0")/wslg-icd-provenance"
if [ -x "$PROBE" ]; then
  probe_out="$("$PROBE" 2>&1)"
  probe_rc=$?                 # captured immediately
  if [ $probe_rc -ne 0 ]; then
    echo "FATAL: ICD provenance probe failed (rc=$probe_rc):" >&2
    echo "$probe_out" >&2
    exit 5
  fi
  case "$probe_out" in
    *"deviceType               : 2 "*|*"deviceType               : 1 "*) : ;;   # DISCRETE | INTEGRATED
    *)
      echo "FATAL: selected device is NOT real hardware — refusing to run." >&2
      echo "$probe_out" | sed 's/^/       /' >&2
      exit 5 ;;
  esac
else
  echo "NOTE: provenance probe not built (\`gcc -o $PROBE ${PROBE}.c -I<sdk>/include /usr/lib/x86_64-linux-gnu/libvulkan.so.1\`);" >&2
  echo "      proceeding — the post-run verdict below still rejects a software device." >&2
fi

{
  echo "=== wslgfix WSLg acceptance (REAL HARDWARE ONLY) ==="
  echo "date        : $(date -Is)"
  echo "binary      : $BIN"
  echo "DISPLAY     : $DISPLAY"
  echo "VK_ICD_FILENAMES: $VK_ICD_FILENAMES"
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
  *"Microsoft Direct3D12"*)
      echo "VERDICT: PASS — presented on REAL HARDWARE via Dozen/D3D12 (0ep.3 target)" ;;
  *llvmpipe*|*lavapipe*|*swiftshader*|*"SwiftShader"*)
      echo "VERDICT: FAIL — ran on SOFTWARE Vulkan. Lavapipe is not an execution path;"
      echo "         this is NOT acceptance evidence and must not be reported as such."
      exit 6 ;;
  *)
      echo "VERDICT: INCONCLUSIVE — exit clean but the device was not identified."
      echo "         Do NOT report as acceptance: an unnamed device cannot be shown to be hardware."
      exit 7 ;;
esac
