#pragma once

#include "CacheCodec.h"

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>
#include <vulkan/vulkan.h>

namespace CashSystem {

// Driver-compiled VkPipelineCache blobs outlive a build tree: a fresh build, worktree or exe path
// starts with an empty per-tree cache, yet the blob is valid for the same device and driver. This
// store keeps one blob per (device, driver, kind) under Vixen::SharedDeviceCacheDirectory().
// Every load is rejected, not trusted, unless the envelope identity (vendor, device, driver
// version, pipelineCacheUUID), checksum and the Vulkan header all match this device.

uint64_t SharedPipelineCacheIdentity(const VkPhysicalDeviceProperties& props) noexcept;

std::filesystem::path SharedPipelineCachePath(const VkPhysicalDeviceProperties& props, std::string_view kind);

CacheFileStatus LoadSharedPipelineCacheBlob(
    const std::filesystem::path& path,
    const VkPhysicalDeviceProperties& props,
    std::string_view kind,
    std::vector<uint8_t>& blob,
    std::string* reason = nullptr);

bool SaveSharedPipelineCacheBlob(
    const std::filesystem::path& path,
    const VkPhysicalDeviceProperties& props,
    std::string_view kind,
    const std::vector<uint8_t>& blob,
    std::string* reason = nullptr);

// Reads the cache's current data and stores it. False when the cache is empty or the write fails.
bool PersistPipelineCacheToSharedStore(
    VkDevice device,
    VkPipelineCache cache,
    const VkPhysicalDeviceProperties& props,
    std::string_view kind,
    std::string* reason = nullptr);

// A cache seeded from the shared store, or VK_NULL_HANDLE when there is no valid blob.
VkPipelineCache CreatePipelineCacheFromSharedStore(
    VkDevice device,
    const VkPhysicalDeviceProperties& props,
    std::string_view kind,
    std::string* reason = nullptr);

} // namespace CashSystem
