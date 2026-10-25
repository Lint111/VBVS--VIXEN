#include "pch.h"
#include "SharedPipelineCacheStore.h"
#include "PipelineCacher.h"
#include "RuntimeCachePaths.h"

#include <cstdio>
#include <cstring>

namespace CashSystem {

namespace {

constexpr std::string_view kEnvelopeType = "shared-vk-pipeline-cache";

void SetReason(std::string* reason, std::string text) {
    if (reason) *reason = std::move(text);
}

} // namespace

uint64_t SharedPipelineCacheIdentity(const VkPhysicalDeviceProperties& props) noexcept {
    struct Identity {
        uint32_t vendorID;
        uint32_t deviceID;
        uint32_t driverVersion;
        uint8_t pipelineCacheUUID[VK_UUID_SIZE];
    } identity{};
    identity.vendorID = props.vendorID;
    identity.deviceID = props.deviceID;
    identity.driverVersion = props.driverVersion;
    std::memcpy(identity.pipelineCacheUUID, props.pipelineCacheUUID, VK_UUID_SIZE);
    return CacheChecksum64(&identity, sizeof(identity));
}

std::filesystem::path SharedPipelineCachePath(const VkPhysicalDeviceProperties& props, std::string_view kind) {
    char deviceDirectory[48];
    std::snprintf(deviceDirectory, sizeof(deviceDirectory), "Device_%08x_%08x", props.vendorID, props.deviceID);
    return ::Vixen::SharedDeviceCacheDirectory() / "pipeline-cache" / deviceDirectory / (std::string(kind) + ".bin");
}

CacheFileStatus LoadSharedPipelineCacheBlob(
    const std::filesystem::path& path,
    const VkPhysicalDeviceProperties& props,
    std::string_view kind,
    std::vector<uint8_t>& blob,
    std::string* reason
) {
    blob.clear();
    std::vector<char> payload;
    std::string error;
    const auto status = ReadCacheEnvelopeBytes(
        path, SharedPipelineCacheIdentity(props), std::string(kEnvelopeType) + ":" + std::string(kind), payload, &error);
    if (status != CacheFileStatus::Loaded) {
        SetReason(reason, error);
        return status;
    }

    std::vector<uint8_t> candidate(payload.begin(), payload.end());
    if (!PipelineCacheBlobMatchesDevice(candidate, props)) {
        SetReason(reason, "pipeline cache header does not match this device/driver");
        return CacheFileStatus::Rejected;
    }
    blob = std::move(candidate);
    return CacheFileStatus::Loaded;
}

bool SaveSharedPipelineCacheBlob(
    const std::filesystem::path& path,
    const VkPhysicalDeviceProperties& props,
    std::string_view kind,
    const std::vector<uint8_t>& blob,
    std::string* reason
) {
    if (!PipelineCacheBlobMatchesDevice(blob, props)) {
        SetReason(reason, "refusing to store a blob whose header does not match this device/driver");
        return false;
    }
    const std::vector<char> payload(blob.begin(), blob.end());
    std::string error;
    if (!WriteCacheEnvelopeBytesAtomically(
            path, SharedPipelineCacheIdentity(props), std::string(kEnvelopeType) + ":" + std::string(kind), payload, &error)) {
        SetReason(reason, error);
        return false;
    }
    return true;
}

bool PersistPipelineCacheToSharedStore(
    VkDevice device,
    VkPipelineCache cache,
    const VkPhysicalDeviceProperties& props,
    std::string_view kind,
    std::string* reason
) {
    if (device == VK_NULL_HANDLE || cache == VK_NULL_HANDLE) {
        SetReason(reason, "no pipeline cache to persist");
        return false;
    }
    size_t size = 0;
    if (vkGetPipelineCacheData(device, cache, &size, nullptr) != VK_SUCCESS || size == 0) {
        SetReason(reason, "pipeline cache has no data");
        return false;
    }
    std::vector<uint8_t> blob(size);
    if (vkGetPipelineCacheData(device, cache, &size, blob.data()) != VK_SUCCESS) {
        SetReason(reason, "vkGetPipelineCacheData failed");
        return false;
    }
    blob.resize(size);
    return SaveSharedPipelineCacheBlob(SharedPipelineCachePath(props, kind), props, kind, blob, reason);
}

VkPipelineCache CreatePipelineCacheFromSharedStore(
    VkDevice device,
    const VkPhysicalDeviceProperties& props,
    std::string_view kind,
    std::string* reason
) {
    std::vector<uint8_t> blob;
    if (LoadSharedPipelineCacheBlob(SharedPipelineCachePath(props, kind), props, kind, blob, reason) !=
        CacheFileStatus::Loaded) {
        return VK_NULL_HANDLE;
    }
    VkPipelineCacheCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    info.initialDataSize = blob.size();
    info.pInitialData = blob.data();
    VkPipelineCache cache = VK_NULL_HANDLE;
    if (vkCreatePipelineCache(device, &info, nullptr, &cache) != VK_SUCCESS) {
        SetReason(reason, "vkCreatePipelineCache rejected the stored blob");
        return VK_NULL_HANDLE;
    }
    return cache;
}

} // namespace CashSystem
