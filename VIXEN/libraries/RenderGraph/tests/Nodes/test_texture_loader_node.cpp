#include <gtest/gtest.h>

#include "Data/Nodes/TextureLoaderNodeConfig.h"
#include "Nodes/TextureLoaderNode.h"

#include <utility>
#include <type_traits>

using namespace Vixen::RenderGraph;

TEST(TextureLoaderNodeBinding, KeepsTheGraphOutputContract) {
    EXPECT_EQ(TextureLoaderNodeConfig::INPUT_COUNT, 1u);
    EXPECT_EQ(TextureLoaderNodeConfig::OUTPUT_COUNT, 5u);
    EXPECT_EQ(TextureLoaderNodeConfig::TEXTURE_IMAGE_Slot::index, 0u);
    EXPECT_EQ(TextureLoaderNodeConfig::TEXTURE_VIEW_Slot::index, 1u);
    EXPECT_EQ(TextureLoaderNodeConfig::TEXTURE_SAMPLER_Slot::index, 2u);
    EXPECT_EQ(TextureLoaderNodeConfig::VULKAN_DEVICE_OUT_Slot::index, 3u);
    EXPECT_EQ(TextureLoaderNodeConfig::TEXTURE_SAMPLER_PAIR_Slot::index, 4u);
}

TEST(TextureLoaderNodeBinding, ModuleResultCarriesTheExistingCacheResources) {
    static_assert(std::is_same_v<
        decltype(std::declval<Vixen::Textures::ResidentTexture>().texture),
        std::shared_ptr<CashSystem::TextureWrapper>>);
    static_assert(std::is_same_v<
        decltype(std::declval<Vixen::Textures::ResidentTexture>().sampler),
        std::shared_ptr<CashSystem::SamplerWrapper>>);

    EXPECT_TRUE((std::is_same_v<
        TextureLoaderNodeConfig::TEXTURE_SAMPLER_PAIR_Slot::Type,
        ImageSamplerPair>));
}
