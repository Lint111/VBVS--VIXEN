#include "Textures/Residency.h"

#include "MainCacher.h"
#include "SamplerCacher.h"
#include "VulkanDevice.h"

#include <stdexcept>
#include <typeindex>
#include <utility>

namespace Vixen::Textures {

TextureResidency::TextureResidency(
    CashSystem::MainCacher& mainCacher,
    Vixen::Vulkan::Resources::VulkanDevice* device)
    : mainCacher_(mainCacher), device_(device) {
    if (device_ == nullptr) {
        throw std::runtime_error("TextureResidency: invalid device handle");
    }
    RegisterCachers();
}

void TextureResidency::RegisterCachers() {
    if (!mainCacher_.IsRegistered(typeid(CashSystem::TextureWrapper))) {
        mainCacher_.RegisterCacher<
            CashSystem::TextureCacher,
            CashSystem::TextureWrapper,
            CashSystem::TextureCreateParams
        >(typeid(CashSystem::TextureWrapper), "Texture", true);
    }

    if (!mainCacher_.IsRegistered(typeid(CashSystem::SamplerWrapper))) {
        mainCacher_.RegisterCacher<
            CashSystem::SamplerCacher,
            CashSystem::SamplerWrapper,
            CashSystem::SamplerCreateParams
        >(typeid(CashSystem::SamplerWrapper), "Sampler", true);
    }
}

ResidentTexture TextureResidency::Load(const std::string& filePath) {
    auto* samplerCacher = mainCacher_.GetCacher<
        CashSystem::SamplerCacher,
        CashSystem::SamplerWrapper,
        CashSystem::SamplerCreateParams
    >(typeid(CashSystem::SamplerWrapper), device_);
    if (samplerCacher == nullptr) {
        throw std::runtime_error("TextureResidency: failed to get SamplerCacher");
    }

    CashSystem::SamplerCreateParams samplerParams{};
    samplerParams.minFilter = VK_FILTER_LINEAR;
    samplerParams.magFilter = VK_FILTER_LINEAR;
    samplerParams.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerParams.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerParams.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerParams.maxAnisotropy = 16.0f;
    samplerParams.compareEnable = VK_FALSE;
    samplerParams.compareOp = VK_COMPARE_OP_NEVER;

    auto sampler = samplerCacher->GetOrCreate(samplerParams);
    if (!sampler || sampler->resource == VK_NULL_HANDLE) {
        throw std::runtime_error("TextureResidency: failed to create sampler");
    }

    auto* textureCacher = mainCacher_.GetCacher<
        CashSystem::TextureCacher,
        CashSystem::TextureWrapper,
        CashSystem::TextureCreateParams
    >(typeid(CashSystem::TextureWrapper), device_);
    if (textureCacher == nullptr) {
        throw std::runtime_error("TextureResidency: failed to get TextureCacher");
    }

    CashSystem::TextureCreateParams textureParams{};
    textureParams.filePath = filePath;
    textureParams.format = VK_FORMAT_R8G8B8A8_UNORM;
    textureParams.samplerWrapper = sampler;

    auto texture = textureCacher->GetOrCreate(textureParams);
    if (!texture || texture->image == VK_NULL_HANDLE) {
        throw std::runtime_error("TextureResidency: failed to create texture");
    }

    return ResidentTexture{std::move(texture), std::move(sampler)};
}

} // namespace Vixen::Textures
