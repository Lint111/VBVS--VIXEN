#pragma once

#include <vector>
#include <vulkan/vulkan.h>

#if defined(__linux__)
#include <cstdlib>
#include <filesystem>
#endif

// Provide weak definitions for global Vulkan option lists.
// Using C++17 inline variables ensures single definition across translation units.
// Note: __declspec(selectany) doesn't work with non-trivial types like std::vector.

inline std::vector<const char*> deviceExtensionNames;
inline std::vector<const char*> layerNames;
inline std::vector<const char*> instanceExtensionNames;

// NOTE (multi-device correctness): the synchronization2 entry points (vkCmdPipelineBarrier2KHR /
// vkQueueSubmit2KHR) previously lived here as process-global function pointers. They are
// DEVICE-LEVEL dispatch pointers, so globals break any scenario with more than one live VkDevice
// and leave a stale-dispatch window during device-loss recovery. They now live as per-instance
// members on VulkanDevice (fpCmdPipelineBarrier2 / fpQueueSubmit2, resolved in CreateDevice) —
// call sites reach them through their VulkanDevice*, or receive the PFN as an explicit parameter
// where no device reference is in scope (BatchedUpdater::RecordAll, PassRecorder).

// On WSL2 the real GPU is reachable only via Mesa Dozen (Vulkan-over-D3D12); the loader's default
// ICD discovery finds only software Vulkan (lavapipe/llvmpipe), which is why letting this go unset
// silently downgrades every WSL2 binary to a CPU rasterizer with no error -- exactly the kind of
// hidden fallback that hides real GPU-path bugs. Every VIXEN executable MUST call this (and
// VixenSelectValidationLayerPath below) at the very top of main(), before any Vulkan instance is
// created by ANY code path in that binary (InstanceNode, a raw vkCreateInstance, etc.) -- there is
// no single shared entry point across binaries, so each main() is responsible for calling both.
//
// If the build provisioned Dozen (the VIXEN_WSL_DZN_ICD compile-def, set by
// cmake/ProvisionWslVulkan.cmake) and the user hasn't already chosen an ICD, point the Vulkan loader
// at it. No-op off WSL (no /dev/dxg), when VK_ICD_FILENAMES is already set, or when the manifest is
// missing (falls back to software Vulkan rather than failing outright -- see ProvisionWslVulkan.cmake
// for how to make that fallback loud instead of silent). libd3d12.so is already on the loader path
// via WSL's ld.wsl.conf, so no LD_LIBRARY_PATH change is needed. Native (non-WSL) hosts have no
// /dev/dxg, so this never alters their behaviour. Returns the ICD path it selected, or nullptr.
inline const char* VixenSelectWslGpuIcd() {
#if defined(__linux__) && defined(VIXEN_WSL_DZN_ICD)
    const char* icd = VIXEN_WSL_DZN_ICD;
    if (icd && icd[0] != '\0'
        && std::filesystem::exists("/dev/dxg")
        && std::getenv("VK_ICD_FILENAMES") == nullptr
        && std::filesystem::exists(icd)) {
        ::setenv("VK_ICD_FILENAMES", icd, /*overwrite=*/0);
        return icd;
    }
#endif
    return nullptr;
}

// When the build enabled validation (VIXEN_VULKAN_VALIDATION) with an auto-provisioned SDK, point the
// Vulkan loader at the provisioned validation-layer manifests (VK_LAYER_PATH) and force-enable the
// layer (VK_INSTANCE_LAYERS) before instance creation -- self-contained, no manual env. The validation
// layer catches invalid GPU ops CPU-side (a log, not a kernel panic -- critical on WSL/Dozen). Same
// call-before-any-instance-creation contract as VixenSelectWslGpuIcd above. Returns the path or null.
inline const char* VixenSelectValidationLayerPath() {
#if defined(__linux__) && defined(VIXEN_VK_LAYER_PATH)
    const char* lp = VIXEN_VK_LAYER_PATH;
    if (lp && lp[0] != '\0' && std::filesystem::exists(lp)) {
        if (std::getenv("VK_LAYER_PATH") == nullptr) ::setenv("VK_LAYER_PATH", lp, /*overwrite=*/0);
        if (std::getenv("VK_INSTANCE_LAYERS") == nullptr)
            ::setenv("VK_INSTANCE_LAYERS", "VK_LAYER_KHRONOS_validation", /*overwrite=*/0);
        return lp;
    }
#endif
    return nullptr;
}
