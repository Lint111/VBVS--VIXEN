#pragma once

#include "Headers.h"
#include "Textures/TextureCacher.h"

#include <memory>
#include <string>

namespace Vixen::Vulkan::Resources {
class VulkanDevice;
}

namespace Vixen::Textures {

// The node-facing result is deliberately just the existing Vulkan resources and
// cache ownership. It is not a second texture model; kernel-generated texture
// descriptors remain the model in later slices.
struct ResidentTexture {
    std::shared_ptr<CashSystem::TextureWrapper> texture;
    std::shared_ptr<CashSystem::SamplerWrapper> sampler;
};

class TextureResidency {
public:
    TextureResidency(
        CashSystem::MainCacher& mainCacher,
        Vixen::Vulkan::Resources::VulkanDevice* device);

    ResidentTexture Load(const std::string& filePath);

private:
    void RegisterCachers();

    CashSystem::MainCacher& mainCacher_;
    Vixen::Vulkan::Resources::VulkanDevice* device_ = nullptr;
};

} // namespace Vixen::Textures
