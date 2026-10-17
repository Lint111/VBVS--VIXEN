#include <FidelityFX/host/backends/vk/ffx_vk.h>

// FidelityFX SDK 1.1.4's Vulkan backend installs this callback unconditionally from
// ffxGetInterfaceVK, but the callback implementation is supplied by its frame-generation
// integration, which VIXEN intentionally does not build. Keep the backend interface complete
// while failing closed if a caller ever requests that unsupported operation.
FfxErrorCode ffxSetFrameGenerationConfigToSwapchainVK(
    FfxFrameGenerationConfig const* config) {
    (void)config;
    return FFX_ERROR_BACKEND_API_ERROR;
}
