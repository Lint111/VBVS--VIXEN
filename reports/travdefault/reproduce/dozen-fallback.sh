#!/usr/bin/env bash
set -euo pipefail

repo_root=$(git rev-parse --show-toplevel)
vixen_root="$repo_root/VIXEN"
build_root="$repo_root/build/wsl"
capture_dir="$build_root/runtime-captures/travdefault-dda-fallback"
log_file="$build_root/runtime-captures/travdefault-dda-fallback.log"
mkdir -p "$capture_dir"

cache_value() {
  sed -n "s/^$1:[^=]*=//p" "$build_root/CMakeCache.txt" | head -n 1
}

dzn_lib_dir=$(cache_value VIXEN_WSL_DZN_LIBRARY_DIR)
windowing_cache=$(cache_value VIXEN_WINDOWING_CACHE_DIR)
vulkan_include=$(cache_value Vulkan_INCLUDE_DIR)
if [[ -z "$dzn_lib_dir" || -z "$windowing_cache" || -z "$vulkan_include" ]]; then
  echo "Missing Dzn runtime paths in $build_root/CMakeCache.txt" >&2
  exit 1
fi
vulkan_sdk="${vulkan_include%/include}"
if [[ "$vulkan_sdk" == "$vulkan_include" ]]; then
  echo "Unexpected Vulkan include path in $build_root/CMakeCache.txt: $vulkan_include" >&2
  exit 1
fi
runtime_lib_path="$dzn_lib_dir:$windowing_cache/usr/lib/x86_64-linux-gnu:$windowing_cache/usr/lib:$vulkan_sdk/lib/VulkanLoader/lib:$vulkan_sdk/lib:/usr/lib/wsl/lib:/home/liory/.local/lib"
if [[ -n "${LD_LIBRARY_PATH:-}" ]]; then
  runtime_lib_path="$runtime_lib_path:$LD_LIBRARY_PATH"
fi

(
  cd "$vixen_root"
  env -u VIXEN_COMPOSED_TRAVERSAL -u VIXEN_RTQUERY_TRAVERSAL \
      -u VIXEN_BRICKMAP_TRAVERSAL \
      LD_LIBRARY_PATH="$runtime_lib_path" \
      VK_ICD_FILENAMES=/home/liory/.cache/vixen/wsl-vulkan/dzn_icd.json \
      VK_LAYER_PATH="$vixen_root/.vulkan-sdk/1.4.350.1/x86_64/share/vulkan/explicit_layer.d" \
      VIXEN_BRICKMAP_SCENE=1 \
      VIXEN_LOOKDEV_CAPTURE_STATE=midday \
      VIXEN_LOOKDEV_CAPTURE_FRAMES=1 \
      VIXEN_LOOKDEV_CAPTURE_DIR="$capture_dir" \
      VIXEN_WINDOW_WIDTH=500 VIXEN_WINDOW_HEIGHT=500 \
      "$build_root/application/main/vixen_lookdev_capture" \
      --gtest_filter=LookdevCapture.RendersFourStatesAtFourAnglesDeterministically
) >"$log_file" 2>&1 &
test_pid=$!

cleanup() {
  kill "$heartbeat_pid" 2>/dev/null || true
  wait "$heartbeat_pid" 2>/dev/null || true
}
heartbeat_pid=
trap cleanup EXIT

(
  while kill -0 "$test_pid" 2>/dev/null; do
    sleep 30
    if kill -0 "$test_pid" 2>/dev/null; then
      echo "[heartbeat] travdefault Dozen fallback capture active $(date -Is)"
    fi
  done
) &
heartbeat_pid=$!

set +e
wait "$test_pid"
test_rc=$?
set -e

rg -e '\[ComposedBackend\]' -e 'RayQueryLighting extensions not available' \
   -e '\[       OK\]' -e '\[  FAILED' -e '\[==========\]' "$log_file" || true
exit "$test_rc"
