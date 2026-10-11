export VK_ICD_FILENAMES=/home/liory/.cache/vixen/wsl-vulkan/dzn_icd.json
export LD_LIBRARY_PATH="$PWD/VIXEN/.vulkan-sdk/1.4.350.1/x86_64/lib/VulkanLoader/lib:$PWD/VIXEN/.vulkan-sdk/1.4.350.1/x86_64/lib:/home/liory/.cache/vixen/wsl-vulkan/mesa/build/src/microsoft/vulkan:$PWD/VIXEN/.windowing-deps/usr/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH:-}"
export DISPLAY=:0
unset WAYLAND_DISPLAY
