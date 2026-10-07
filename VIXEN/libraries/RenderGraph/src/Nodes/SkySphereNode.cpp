// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.

#include "Nodes/SkySphereNode.h"
#include "Core/NodeRegistration.h"
#include "Core/NodeLogging.h"
#include "VulkanDevice.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <stdexcept>

namespace {

using Vixen::RenderGraph::SkySphereStar;

constexpr float kPi = 3.14159265358979323846f;
constexpr uint32_t kProceduralStarCount = 1536;
constexpr float kDefaultBrightness = 0.45f;
constexpr float kMaximumBrightness = 2.0f;
constexpr float kGalaxyBandGain = 0.065f;
constexpr float kStarGain = 0.30f;

struct Vec3 {
    float x;
    float y;
    float z;
};

uint32_t Mix32(uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

float UnitFloat(uint32_t value) {
    return static_cast<float>(value >> 8) * (1.0f / 16777216.0f);
}

uint64_t HashStarList(const std::vector<SkySphereStar>& stars) {
    uint64_t hash = 1469598103934665603ull;
    const auto add = [&hash](uint32_t value) {
        hash ^= value;
        hash *= 1099511628211ull;
    };

    add(static_cast<uint32_t>(stars.size()));
    for (const SkySphereStar& star : stars) {
        add(std::bit_cast<uint32_t>(star.direction[0]));
        add(std::bit_cast<uint32_t>(star.direction[1]));
        add(std::bit_cast<uint32_t>(star.direction[2]));
        add(std::bit_cast<uint32_t>(star.brightness));
    }
    return hash;
}

Vec3 Normalize(Vec3 value) {
    const float lengthSquared = value.x * value.x + value.y * value.y + value.z * value.z;
    if (!(lengthSquared > 0.0f) || !std::isfinite(lengthSquared)) return {0.0f, 0.0f, 1.0f};
    const float inverseLength = 1.0f / std::sqrt(lengthSquared);
    return {value.x * inverseLength, value.y * inverseLength, value.z * inverseLength};
}

// Matches SkySphereAccumulate.comp's octahedral UV -> direction mapping.
Vec3 OctDecode(float u, float v) {
    const float x = u * 2.0f - 1.0f;
    const float y = v * 2.0f - 1.0f;
    Vec3 n{x, y, 1.0f - std::abs(x) - std::abs(y)};
    const float t = std::clamp(-n.z, 0.0f, 1.0f);
    n.x += n.x >= 0.0f ? -t : t;
    n.y += n.y >= 0.0f ? -t : t;
    return Normalize(n);
}

std::pair<float, float> OctEncode(Vec3 direction) {
    direction = Normalize(direction);
    const float inverseL1 = 1.0f /
        (std::abs(direction.x) + std::abs(direction.y) + std::abs(direction.z));
    float x = direction.x * inverseL1;
    float y = direction.y * inverseL1;
    if (direction.z < 0.0f) {
        const float oldX = x;
        x = (1.0f - std::abs(y)) * (oldX >= 0.0f ? 1.0f : -1.0f);
        y = (1.0f - std::abs(oldX)) * (y >= 0.0f ? 1.0f : -1.0f);
    }
    return {x * 0.5f + 0.5f, y * 0.5f + 0.5f};
}

uint16_t FloatToHalf(float value) {
    const uint32_t bits = std::bit_cast<uint32_t>(value);
    const uint32_t sign = (bits >> 16) & 0x8000u;
    const uint32_t exponent = (bits >> 23) & 0xffu;
    uint32_t mantissa = bits & 0x7fffffu;

    if (exponent == 0xffu) {
        return static_cast<uint16_t>(sign | (mantissa == 0 ? 0x7c00u : 0x7e00u));
    }

    int32_t halfExponent = static_cast<int32_t>(exponent) - 127 + 15;
    if (halfExponent <= 0) {
        if (halfExponent < -10) return static_cast<uint16_t>(sign);
        mantissa |= 0x800000u;
        const uint32_t shift = static_cast<uint32_t>(14 - halfExponent);
        const uint32_t rounded = mantissa + ((1u << (shift - 1)) - 1u) + ((mantissa >> shift) & 1u);
        return static_cast<uint16_t>(sign | (rounded >> shift));
    }
    if (halfExponent >= 31) return static_cast<uint16_t>(sign | 0x7c00u);

    mantissa += 0xfffu + ((mantissa >> 13) & 1u);
    if ((mantissa & 0x800000u) != 0) {
        mantissa = 0;
        ++halfExponent;
        if (halfExponent >= 31) return static_cast<uint16_t>(sign | 0x7c00u);
    }
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(halfExponent) << 10) |
                                 (mantissa >> 13));
}

uint32_t FindSuitableMemoryType(
    const VkPhysicalDeviceMemoryProperties& memProps,
    uint32_t typeFilter,
    VkMemoryPropertyFlags required,
    const char* context)
{
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & required) == required) {
            return i;
        }
    }
    throw std::runtime_error(std::string("[SkySphereNode] No suitable memory type for ") + context);
}

struct StagingBuffer {
    VkDevice device = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;

    ~StagingBuffer() {
        if (buffer != VK_NULL_HANDLE) vkDestroyBuffer(device, buffer, nullptr);
        if (memory != VK_NULL_HANDLE) vkFreeMemory(device, memory, nullptr);
    }
};

std::vector<uint16_t> GenerateSkyPixels(uint32_t width, uint32_t height, uint32_t seed,
                                        float brightness,
                                        const std::vector<SkySphereStar>& extraStars) {
    const size_t pixelCount = static_cast<size_t>(width) * height;
    std::vector<float> linear(pixelCount * 4, 0.0f);

    // This muted blue-violet band is linear radiance, capped below 0.030 at the default 0.45
    // brightness. It is intentionally broad and soft so body lighting remains the scene's
    // dominant two-mode signal.
    const Vec3 galaxyNormal = Normalize({0.31f, 0.87f, -0.38f});
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const size_t pixel = static_cast<size_t>(y) * width + x;
            const Vec3 direction = OctDecode((static_cast<float>(x) + 0.5f) / width,
                                             (static_cast<float>(y) + 0.5f) / height);
            const float latitude = std::abs(direction.x * galaxyNormal.x +
                                            direction.y * galaxyNormal.y +
                                            direction.z * galaxyNormal.z);
            const float bandEdge = std::max(0.0f, 1.0f - latitude / 0.17f);
            const float band = bandEdge * bandEdge;
            const float variation = 0.82f + 0.18f * UnitFloat(Mix32(seed ^
                (x * 0x9e3779b9u) ^ (y * 0x85ebca6bu)));
            const float gain = kGalaxyBandGain * brightness * band * variation;
            linear[pixel * 4 + 0] = gain * 0.58f;
            linear[pixel * 4 + 1] = gain * 0.52f;
            linear[pixel * 4 + 2] = gain;
            linear[pixel * 4 + 3] = 1.0f;
        }
    }

    const auto addStar = [&](Vec3 direction, float relativeBrightness, uint32_t colorSeed) {
        direction = Normalize(direction);
        if (!std::isfinite(relativeBrightness) || relativeBrightness <= 0.0f) return;
        const auto [u, v] = OctEncode(direction);
        const uint32_t x = std::min(width - 1, static_cast<uint32_t>(u * width));
        const uint32_t y = std::min(height - 1, static_cast<uint32_t>(v * height));
        const size_t pixel = (static_cast<size_t>(y) * width + x) * 4;

        // Two fixed linear-light star tints: cool white and warm white. A single sparse texel
        // keeps the stars pin-point; the 0.30 peak constant yields at most 0.135 linear radiance
        // after the default 0.45 brightness scale.
        const float colorMix = UnitFloat(Mix32(colorSeed ^ 0xc2b2ae35u));
        const float temperature = std::clamp(colorMix, 0.0f, 1.0f);
        const float warm[3] = {1.0f, 0.86f, 0.72f};
        const float cool[3] = {0.76f, 0.87f, 1.0f};
        const float randomMagnitude = 0.72f + 0.28f * UnitFloat(Mix32(colorSeed ^ 0x27d4eb2fu));
        const float gain = kStarGain * brightness *
            std::clamp(relativeBrightness, 0.0f, 4.0f) * randomMagnitude;
        for (size_t channel = 0; channel < 3; ++channel) {
            const float tint = warm[channel] * (1.0f - temperature) + cool[channel] * temperature;
            linear[pixel + channel] += gain * tint;
        }
    };

    for (uint32_t i = 0; i < kProceduralStarCount; ++i) {
        const uint32_t h0 = Mix32(seed ^ (i * 0x9e3779b9u));
        const uint32_t h1 = Mix32(h0 + 0x68bc21ebu);
        const uint32_t h2 = Mix32(h1 + 0x02e5be93u);
        const float z = 1.0f - 2.0f * (static_cast<float>(i) + UnitFloat(h0)) /
                                 static_cast<float>(kProceduralStarCount);
        const float phi = 2.0f * kPi * UnitFloat(h1);
        const float radial = std::sqrt(std::max(0.0f, 1.0f - z * z));
        const Vec3 direction{radial * std::cos(phi), radial * std::sin(phi), z};
        const float relativeBrightness = 0.55f + 0.45f * UnitFloat(h2);
        addStar(direction, relativeBrightness, h2);
    }

    for (size_t i = 0; i < extraStars.size(); ++i) {
        const SkySphereStar& star = extraStars[i];
        addStar({star.direction[0], star.direction[1], star.direction[2]},
                star.brightness, seed ^ static_cast<uint32_t>(i));
    }

    std::vector<uint16_t> half(pixelCount * 4);
    std::transform(linear.begin(), linear.end(), half.begin(), FloatToHalf);
    return half;
}

} // namespace

namespace Vixen::RenderGraph {

using namespace Vixen::Vulkan::Resources;

std::unique_ptr<NodeInstance> SkySphereNodeType::CreateInstance(const std::string& name) const {
    return std::make_unique<SkySphereNode>(name, const_cast<SkySphereNodeType*>(this));
}

SkySphereNode::SkySphereNode(const std::string& name, NodeType* type)
    : TypedNode<SkySphereNodeConfig>(name, type) {
}

void SkySphereNode::TypedSetupImpl(TypedSetupContext&) {
    width_ = GetParameterValue<uint32_t>(SkySphereNodeConfig::PARAM_WIDTH, 0u);
    height_ = GetParameterValue<uint32_t>(SkySphereNodeConfig::PARAM_HEIGHT, 0u);
    format_ = static_cast<VkFormat>(GetParameterValue<uint32_t>(
        SkySphereNodeConfig::PARAM_FORMAT,
        static_cast<uint32_t>(VK_FORMAT_R16G16B16A16_SFLOAT)));
    refreshCadenceFrames_ = GetParameterValue<uint32_t>(
        SkySphereNodeConfig::PARAM_REFRESH_CADENCE_FRAMES, 0u);

    NODE_LOG_DEBUG("[SkySphereNode] Setup: " + std::to_string(width_) + "x" +
                   std::to_string(height_) + " format=" +
                   std::to_string(static_cast<int>(format_)));
}

void SkySphereNode::TypedCompileImpl(TypedCompileContext& ctx) {
    device_ = ctx.In(SkySphereNodeConfig::VULKAN_DEVICE_IN);
    commandPool_ = ctx.In(SkySphereNodeConfig::COMMAND_POOL);
    if (!device_) throw std::runtime_error("[SkySphereNode] VULKAN_DEVICE_IN is null");
    if (commandPool_ == VK_NULL_HANDLE) throw std::runtime_error("[SkySphereNode] COMMAND_POOL is null");
    if (width_ == 0 || height_ == 0) {
        throw std::runtime_error("[SkySphereNode] WIDTH/HEIGHT parameters must be > 0");
    }
    if (format_ != VK_FORMAT_R16G16B16A16_SFLOAT) {
        throw std::runtime_error("[SkySphereNode] SpatialReuseShade requires RGBA16F sky format");
    }

    if (target_.buffers.empty()) {
        CreateImage(device_, commandPool_);
    }
    ctx.Out(SkySphereNodeConfig::SKY_SPHERE, static_cast<IRenderTarget*>(&target_));
    ctx.Out(SkySphereNodeConfig::CURRENT_VIEW, target_.GetCurrentView());
}

void SkySphereNode::TypedExecuteImpl(TypedExecuteContext& ctx) {
    const bool enabled = GetParameterValue<uint32_t>(SkySphereNodeConfig::PARAM_ENABLED, 0u) != 0;
    const uint32_t seed = GetParameterValue<uint32_t>(
        SkySphereNodeConfig::PARAM_SEED, 0x5a17f13du);
    const float requestedBrightness = GetParameterValue<float>(
        SkySphereNodeConfig::PARAM_BRIGHTNESS, kDefaultBrightness);
    const float brightness = std::clamp(
        std::isfinite(requestedBrightness) ? requestedBrightness : 0.0f,
        0.0f, kMaximumBrightness);
    refreshCadenceFrames_ = GetParameterValue<uint32_t>(
        SkySphereNodeConfig::PARAM_REFRESH_CADENCE_FRAMES, refreshCadenceFrames_);

    const bool hasStarListInput = NodeInstance::GetInputCount(
        SkySphereNodeConfig::STAR_LIST.index) > 0;
    bool extraStarsChanged = false;
    if (hasStarListInput &&
        (!starListInitialized_ || refreshCadenceFrames_ == 0 ||
         executeFrame_ % refreshCadenceFrames_ == 0)) {
        const SkySphereStarList* suppliedList = ctx.In(SkySphereNodeConfig::STAR_LIST);
        std::vector<SkySphereStar> stars = suppliedList ? suppliedList->stars
                                                       : std::vector<SkySphereStar>{};
        const uint64_t listHash = HashStarList(stars);
        if (!starListInitialized_ || listHash != cachedStarListHash_) {
            cachedExtraStars_ = std::move(stars);
            cachedStarListHash_ = listHash;
            starListInitialized_ = true;
            extraStarsChanged = true;
        }
    } else if (!hasStarListInput && !starListInitialized_) {
        cachedExtraStars_.clear();
        cachedStarListHash_ = HashStarList(cachedExtraStars_);
        starListInitialized_ = true;
    }

    const bool needsRefresh = !imageInitialized_ || enabled != cachedEnabled_ ||
        (enabled && (seed != cachedSeed_ || brightness != cachedBrightness_ || extraStarsChanged));
    if (needsRefresh) {
        RefreshImage(enabled, seed, brightness, cachedExtraStars_);
    }
    ++executeFrame_;

    ctx.Out(SkySphereNodeConfig::SKY_SPHERE, static_cast<IRenderTarget*>(&target_));
    ctx.Out(SkySphereNodeConfig::CURRENT_VIEW, target_.GetCurrentView());
}

void SkySphereNode::TypedCleanupImpl(TypedCleanupContext& ctx) {
    if (ctx.reason == CleanupReason::Recompile) return;
    DestroyImage();
}

void SkySphereNode::CreateImage(VulkanDevice* device, VkCommandPool commandPool) {
    device_ = device;
    commandPool_ = commandPool;

    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(*device_->gpu, &memoryProperties);

    target_.format = format_;
    target_.extent = {width_, height_};
    target_.imageUsageFlags = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    target_.currentIndex = 0;
    target_.buffers.resize(1);
    RenderTargetBuffer& buffer = target_.buffers[0];

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format_;
    imageInfo.extent = {width_, height_, 1u};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(device_->device, &imageInfo, nullptr, &buffer.image) != VK_SUCCESS) {
        throw std::runtime_error("[SkySphereNode] vkCreateImage failed");
    }

    VkMemoryRequirements imageRequirements{};
    vkGetImageMemoryRequirements(device_->device, buffer.image, &imageRequirements);
    VkMemoryAllocateInfo imageAllocation{};
    imageAllocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    imageAllocation.allocationSize = imageRequirements.size;
    imageAllocation.memoryTypeIndex = FindSuitableMemoryType(
        memoryProperties, imageRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "sky image");
    if (vkAllocateMemory(device_->device, &imageAllocation, nullptr, &buffer.memory) != VK_SUCCESS) {
        throw std::runtime_error("[SkySphereNode] vkAllocateMemory for image failed");
    }
    if (vkBindImageMemory(device_->device, buffer.image, buffer.memory, 0) != VK_SUCCESS) {
        throw std::runtime_error("[SkySphereNode] vkBindImageMemory failed");
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = buffer.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format_;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device_->device, &viewInfo, nullptr, &buffer.view) != VK_SUCCESS) {
        throw std::runtime_error("[SkySphereNode] vkCreateImageView failed");
    }

    TransitionToGeneral(commandPool_);
    NODE_LOG_INFO("[SkySphereNode] Created RGBA16F octahedral cache " +
                  std::to_string(width_) + "x" + std::to_string(height_));
}

void SkySphereNode::TransitionToGeneral(VkCommandPool commandPool) {
    VkDevice vkDevice = device_->device;
    VkCommandBufferAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocation.commandPool = commandPool;
    allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocation.commandBufferCount = 1;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(vkDevice, &allocation, &commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("[SkySphereNode] vkAllocateCommandBuffers failed");
    }
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(commandBuffer, &begin) != VK_SUCCESS) {
        vkFreeCommandBuffers(vkDevice, commandPool, 1, &commandBuffer);
        throw std::runtime_error("[SkySphereNode] vkBeginCommandBuffer failed");
    }

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = target_.buffers[0].image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        vkFreeCommandBuffers(vkDevice, commandPool, 1, &commandBuffer);
        throw std::runtime_error("[SkySphereNode] vkEndCommandBuffer failed");
    }
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commandBuffer;
    VkResult result;
    {
        std::lock_guard submitLock(device_->SubmitMutex(device_->queue));
        result = vkQueueSubmit(device_->queue, 1, &submit, VK_NULL_HANDLE);
        if (result == VK_SUCCESS) result = vkQueueWaitIdle(device_->queue);
    }
    vkFreeCommandBuffers(vkDevice, commandPool, 1, &commandBuffer);
    if (result != VK_SUCCESS) throw std::runtime_error("[SkySphereNode] image transition submit failed");
    target_.SetImageLayout(0, VK_IMAGE_LAYOUT_GENERAL);
}

void SkySphereNode::RefreshImage(bool enabled, uint32_t seed, float brightness,
                                 const std::vector<SkySphereStar>& extraStars) {
    if (enabled) {
        const std::vector<uint16_t> pixels = GenerateSkyPixels(width_, height_, seed,
                                                               brightness, extraStars);
        UploadPixels(&pixels, imageInitialized_);
    } else {
        UploadPixels(nullptr, imageInitialized_);
    }
    imageInitialized_ = true;
    cachedEnabled_ = enabled;
    cachedSeed_ = seed;
    cachedBrightness_ = brightness;
}

void SkySphereNode::UploadPixels(const std::vector<uint16_t>* rgba16, bool initialized) {
    VkDevice vkDevice = device_->device;
    const VkDeviceSize byteCount = rgba16
        ? static_cast<VkDeviceSize>(rgba16->size() * sizeof(uint16_t))
        : 0;

    StagingBuffer staging{vkDevice};
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(*device_->gpu, &memoryProperties);

    bool stagingHostCoherent = false;
    if (rgba16) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = byteCount;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(vkDevice, &bufferInfo, nullptr, &staging.buffer) != VK_SUCCESS) {
            throw std::runtime_error("[SkySphereNode] staging vkCreateBuffer failed");
        }

        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(vkDevice, staging.buffer, &requirements);
        uint32_t memoryType = UINT32_MAX;
        for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
            const VkMemoryPropertyFlags flags = memoryProperties.memoryTypes[i].propertyFlags;
            if ((requirements.memoryTypeBits & (1u << i)) &&
                (flags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                    (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
                memoryType = i;
                stagingHostCoherent = true;
                break;
            }
        }
        if (memoryType == UINT32_MAX) {
            memoryType = FindSuitableMemoryType(memoryProperties, requirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, "host-visible staging buffer");
            stagingHostCoherent = (memoryProperties.memoryTypes[memoryType].propertyFlags &
                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
        }

        VkMemoryAllocateInfo memoryAllocation{};
        memoryAllocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memoryAllocation.allocationSize = requirements.size;
        memoryAllocation.memoryTypeIndex = memoryType;
        if (vkAllocateMemory(vkDevice, &memoryAllocation, nullptr, &staging.memory) != VK_SUCCESS) {
            throw std::runtime_error("[SkySphereNode] staging vkAllocateMemory failed");
        }
        if (vkBindBufferMemory(vkDevice, staging.buffer, staging.memory, 0) != VK_SUCCESS) {
            throw std::runtime_error("[SkySphereNode] staging vkBindBufferMemory failed");
        }

        void* mapped = nullptr;
        if (vkMapMemory(vkDevice, staging.memory, 0, byteCount, 0, &mapped) != VK_SUCCESS) {
            throw std::runtime_error("[SkySphereNode] staging vkMapMemory failed");
        }
        std::memcpy(mapped, rgba16->data(), static_cast<size_t>(byteCount));
        if (!stagingHostCoherent) {
            VkMappedMemoryRange range{};
            range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            range.memory = staging.memory;
            range.offset = 0;
            range.size = VK_WHOLE_SIZE;
            if (vkFlushMappedMemoryRanges(vkDevice, 1, &range) != VK_SUCCESS) {
                vkUnmapMemory(vkDevice, staging.memory);
                throw std::runtime_error("[SkySphereNode] staging vkFlushMappedMemoryRanges failed");
            }
        }
        vkUnmapMemory(vkDevice, staging.memory);
    }

    VkCommandBufferAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocation.commandPool = commandPool_;
    allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocation.commandBufferCount = 1;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(vkDevice, &allocation, &commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("[SkySphereNode] upload vkAllocateCommandBuffers failed");
    }

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(commandBuffer, &begin) != VK_SUCCESS) {
        vkFreeCommandBuffers(vkDevice, commandPool_, 1, &commandBuffer);
        throw std::runtime_error("[SkySphereNode] upload vkBeginCommandBuffer failed");
    }

    VkImageMemoryBarrier toTransfer{};
    toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransfer.srcAccessMask = initialized ? VK_ACCESS_SHADER_READ_BIT : 0;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = target_.buffers[0].image;
    toTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(commandBuffer,
        initialized ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toTransfer);

    if (rgba16) {
        VkBufferMemoryBarrier hostWrite{};
        hostWrite.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        hostWrite.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        hostWrite.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        hostWrite.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hostWrite.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hostWrite.buffer = staging.buffer;
        hostWrite.offset = 0;
        hostWrite.size = byteCount;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 1, &hostWrite, 0, nullptr);

        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {width_, height_, 1};
        vkCmdCopyBufferToImage(commandBuffer, staging.buffer, target_.buffers[0].image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    } else {
        VkClearColorValue transparentBlack{};
        vkCmdClearColorImage(commandBuffer, target_.buffers[0].image,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &transparentBlack,
                             1, &toTransfer.subresourceRange);
    }

    VkImageMemoryBarrier toGeneral{};
    toGeneral.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toGeneral.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toGeneral.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toGeneral.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    toGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toGeneral.image = target_.buffers[0].image;
    toGeneral.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &toGeneral);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        vkFreeCommandBuffers(vkDevice, commandPool_, 1, &commandBuffer);
        throw std::runtime_error("[SkySphereNode] upload vkEndCommandBuffer failed");
    }
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commandBuffer;
    VkResult result;
    {
        std::lock_guard submitLock(device_->SubmitMutex(device_->queue));
        result = vkQueueSubmit(device_->queue, 1, &submit, VK_NULL_HANDLE);
        if (result == VK_SUCCESS) result = vkQueueWaitIdle(device_->queue);
    }
    vkFreeCommandBuffers(vkDevice, commandPool_, 1, &commandBuffer);
    if (result != VK_SUCCESS) throw std::runtime_error("[SkySphereNode] upload queue submission failed");
    target_.SetImageLayout(0, VK_IMAGE_LAYOUT_GENERAL);
}

void SkySphereNode::DestroyImage() {
    if (!device_) return;
    VkDevice vkDevice = device_->device;
    for (auto& buffer : target_.buffers) {
        if (buffer.view != VK_NULL_HANDLE) vkDestroyImageView(vkDevice, buffer.view, nullptr);
        if (buffer.image != VK_NULL_HANDLE) vkDestroyImage(vkDevice, buffer.image, nullptr);
        if (buffer.memory != VK_NULL_HANDLE) vkFreeMemory(vkDevice, buffer.memory, nullptr);
        buffer.view = VK_NULL_HANDLE;
        buffer.image = VK_NULL_HANDLE;
        buffer.memory = VK_NULL_HANDLE;
    }
    target_.buffers.clear();
    imageInitialized_ = false;
    device_ = nullptr;
    commandPool_ = VK_NULL_HANDLE;
}

} // namespace Vixen::RenderGraph

VIXEN_REGISTER_NODE(Vixen::RenderGraph::SkySphereNodeType);
