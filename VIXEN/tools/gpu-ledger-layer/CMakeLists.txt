# VK_LAYER_VIXEN_gpu_ledger (sn-gpumem1): per-job GPU residency and per-frame upload ledger. Benchmark tooling: only configured when
# UNDERSET_NATIVE_BENCHMARK is on (see the parent CMakeLists), never part of a normal build or a test.
add_library(vixen_gpu_ledger_layer MODULE gpu_ledger_layer.cpp)
target_compile_features(vixen_gpu_ledger_layer PRIVATE cxx_std_17)
if(TARGET Vulkan::Headers)
    target_link_libraries(vixen_gpu_ledger_layer PRIVATE Vulkan::Headers)
elseif(TARGET Vulkan::Vulkan)
    target_link_libraries(vixen_gpu_ledger_layer PRIVATE Vulkan::Vulkan)
endif()
set(VIXEN_GPU_LEDGER_LAYER_DIR "${CMAKE_BINARY_DIR}/gpu-ledger-layer" CACHE PATH "Where the ledger layer DLL and its manifest are written")
set_target_properties(vixen_gpu_ledger_layer PROPERTIES
    OUTPUT_NAME VkLayer_vixen_gpu_ledger
    LIBRARY_OUTPUT_DIRECTORY "${VIXEN_GPU_LEDGER_LAYER_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${VIXEN_GPU_LEDGER_LAYER_DIR}"
    CXX_VISIBILITY_PRESET hidden)
if(MSVC)
    target_compile_options(vixen_gpu_ledger_layer PRIVATE /W4 /WX)
endif()

file(GENERATE OUTPUT "${VIXEN_GPU_LEDGER_LAYER_DIR}/VkLayer_vixen_gpu_ledger.json" CONTENT [=[{
  "file_format_version": "1.2.0",
  "layer": {
    "name": "VK_LAYER_VIXEN_gpu_ledger",
    "type": "GLOBAL",
    "library_path": ".\\VkLayer_vixen_gpu_ledger.dll",
    "api_version": "1.3.0",
    "implementation_version": "1",
    "description": "VIXEN GPU ledger: resident allocations and per-frame uploads (benchmark tooling)",
    "functions": {
      "vkNegotiateLoaderLayerInterfaceVersion": "vkNegotiateLoaderLayerInterfaceVersion"
    },
    "enable_environment": { "VIXEN_GPU_LEDGER": "1" },
    "disable_environment": { "VIXEN_GPU_LEDGER_DISABLE": "1" }
  }
}
]=])
