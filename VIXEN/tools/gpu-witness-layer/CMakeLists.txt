# Vulkan implicit layer that records and polices GPU use by test processes (see gpu_witness_layer.cpp).
# Linux only: the loader's implicit-layer discovery by environment path is what keeps it test-scoped.
add_library(vixen_gpu_witness_layer MODULE gpu_witness_layer.cpp)
target_compile_features(vixen_gpu_witness_layer PRIVATE cxx_std_17)
if(TARGET Vulkan::Headers)
    target_link_libraries(vixen_gpu_witness_layer PRIVATE Vulkan::Headers)
elseif(VULKAN_PATH)
    target_include_directories(vixen_gpu_witness_layer PRIVATE "${VULKAN_PATH}/include")
endif()
set_target_properties(vixen_gpu_witness_layer PROPERTIES
    OUTPUT_NAME VkLayer_vixen_gpu_witness
    LIBRARY_OUTPUT_DIRECTORY "${VIXEN_GPU_WITNESS_LAYER_DIR}"
    CXX_VISIBILITY_PRESET hidden)

file(GENERATE OUTPUT "${VIXEN_GPU_WITNESS_LAYER_DIR}/VkLayer_vixen_gpu_witness.json" CONTENT [=[{
  "file_format_version": "1.2.0",
  "layer": {
    "name": "VK_LAYER_VIXEN_gpu_witness",
    "type": "GLOBAL",
    "library_path": "./libVkLayer_vixen_gpu_witness.so",
    "api_version": "1.3.0",
    "implementation_version": "1",
    "description": "VIXEN test GPU witness: records GPU actions and fails CPU-classified tests that take them",
    "functions": {
      "vkNegotiateLoaderLayerInterfaceVersion": "vkNegotiateLoaderLayerInterfaceVersion"
    },
    "enable_environment": { "VIXEN_GPU_WITNESS": "1" },
    "disable_environment": { "VIXEN_GPU_WITNESS_DISABLE": "1" }
  }
}
]=])
